import { cp, mkdir, readFile, rm, unlink, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { flag, option, projectPaths, readJson, resolveOutputRoot, writeJson } from "./cli.mjs";

// Validates a project's scripting-api/ with services-cli and lays out the contract the docs site
// consumes. The contract's identity (slug, title, tagline) is the `contract` entry of the
// project's scripting-api/reference.config.json; everything else there is its targets.

// Pinned in the project so a local run and CI agree on the generator without anyone exporting
// anything. The environment variable stays an override, for trying an unreleased build.
const resolveServicesCliVersion = async (apiRoot) => {
  const pinFile = path.join(apiRoot, "services-cli.version");
  const fromEnvironment = process.env.MAFIAHUB_SERVICES_CLI_VERSION?.trim();
  const version = fromEnvironment || (existsSync(pinFile) ? (await readFile(pinFile, "utf8")).trim() : "");
  if (!version) throw new Error("Set MAFIAHUB_SERVICES_CLI_VERSION or pin one in scripting-api/services-cli.version");
  if (!/^[0-9A-Za-z.+-]+$/.test(version)) throw new Error(`Invalid services-cli version '${version}'`);
  return version;
};

const runServicesCli = (repositoryRoot, version, args) => {
  const localEntrypoint = process.env.MAFIAHUB_SERVICES_CLI_ENTRYPOINT;
  const command = localEntrypoint ? process.execPath : "pnpm";
  const commandArguments = localEntrypoint ? [path.resolve(localEntrypoint), ...args] : ["dlx", `@mafiahub/services-cli@${version}`, ...args];
  // pnpm is a .cmd shim on Windows, and Node refuses to spawn one without a shell. Quote the
  // arguments ourselves, because a shell is what splits them once it is asked for.
  const useShell = !localEntrypoint && process.platform === "win32";
  const spawnArguments = useShell ? commandArguments.map((argument) => (/[\s"]/.test(argument) ? JSON.stringify(argument) : argument)) : commandArguments;
  const result = spawnSync(command, spawnArguments, { cwd: repositoryRoot, env: process.env, stdio: "inherit", shell: useShell });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`services-cli exited with status ${result.status}`);
};

// The pinned generator drops readonly on static properties and emits an implicit
// public constructor for native classes with no constructor. Preserve those
// metadata constraints in the declarations we publish.
const preserveClassConstraints = (source, metadata) => {
  const classes = new Map(metadata.symbols.filter((symbol) => symbol.kind === "constructor").map((symbol) => [symbol.name, symbol]));
  return source.replace(/^  class (\w+)([^\n]*)\n([\s\S]*?)^  }/gm, (block, name, declaration, body) => {
    const symbol = classes.get(name);
    if (!symbol) throw new Error(`Missing class metadata for ${name}`);
    const readonly = new Set(symbol.properties.filter((property) => property.static && property.readonly).map((property) => property.name));
    body = body.replace(/^    static (?:readonly )?(\w+)(\??:.*)$/gm, (line, property, type) => (readonly.has(property) ? `    static readonly ${property}${type}` : line));
    if (symbol.constructor === null && !/^    (?:private |protected )?constructor\(/m.test(body)) {
      body = `    private constructor();\n\n${body}`;
    }
    return `  class ${name}${declaration}\n${body}  }`;
  });
};

/**
 * node scripts/build_scripting_contract.mjs --out <directory> [--skip-missing]
 *
 * @param {object} options
 * @param {string} options.projectRoot directory holding scripting-api/
 * @param {boolean} [options.fixClassConstraints] rewrite the generated declarations with preserveClassConstraints
 */
export const buildScriptingContract = async ({ projectRoot, fixClassConstraints = false }) => {
  const { repositoryRoot, apiRoot } = projectPaths(projectRoot);
  const output = option("--out");
  const skipMissing = flag("--skip-missing");
  if (!output) throw new Error("Usage: node scripts/build_scripting_contract.mjs --out <directory> [--skip-missing]");
  const version = await resolveServicesCliVersion(apiRoot);
  const outputRoot = resolveOutputRoot(repositoryRoot, output, "Contract");

  const referenceConfig = await readJson(path.join(apiRoot, "reference.config.json"));
  const { slug, title, tagline } = referenceConfig.contract ?? {};
  if (!slug || !title || !tagline) throw new Error("scripting-api/reference.config.json needs a contract entry with slug, title and tagline");

  // A target's metadata is written by the binary that owns it, so the client's only appears once a
  // client has connected once. --skip-missing lets a validation run cover what is there; a publish
  // must never use it, because half a contract is not a contract.
  if (skipMissing) {
    for (const [name, target] of Object.entries(referenceConfig.targets)) {
      if (!existsSync(path.resolve(apiRoot, target.metadataFile))) {
        console.warn(`Skipping ${name}: ${target.metadataFile} has not been generated yet.`);
        delete referenceConfig.targets[name];
      }
    }
    if (Object.keys(referenceConfig.targets).length === 0) throw new Error("No target has generated metadata; run a debug build of the server or client first");
  }

  const validationConfig = { schemaVersion: 1, slug, title, tagline, targets: referenceConfig.targets };
  const temporaryConfig = path.join(apiRoot, `.contract.validation.${process.pid}.json`);
  try {
    await writeJson(temporaryConfig, validationConfig);
    runServicesCli(repositoryRoot, version, ["docs", "check", "--config", temporaryConfig]);
  } finally {
    await unlink(temporaryConfig).catch(() => {});
  }

  if (fixClassConstraints) {
    for (const target of Object.values(referenceConfig.targets)) {
      const declaration = path.resolve(apiRoot, target.generatedEntryPoint);
      const metadata = await readJson(path.resolve(apiRoot, target.metadataFile));
      await writeFile(declaration, preserveClassConstraints(await readFile(declaration, "utf8"), metadata), "utf8");
    }
  }

  await rm(outputRoot, { recursive: true, force: true });
  await mkdir(path.join(outputRoot, "targets"), { recursive: true });
  await cp(path.join(apiRoot, "shared.d.ts"), path.join(outputRoot, "targets", "shared.d.ts"));

  const publicTargets = {};
  for (const [name, target] of Object.entries(referenceConfig.targets)) {
    const targetRoot = path.join(outputRoot, "targets", name);
    await mkdir(targetRoot, { recursive: true });
    await cp(path.resolve(apiRoot, target.generatedEntryPoint), path.join(targetRoot, "api.d.ts"));
    await cp(path.resolve(apiRoot, target.metadataFile), path.join(targetRoot, "metadata.json"));
    const tsconfig = await readJson(path.resolve(apiRoot, target.tsconfig));
    tsconfig.files = ["../shared.d.ts", "api.d.ts"];
    await writeJson(path.join(targetRoot, "tsconfig.json"), tsconfig);
    publicTargets[name] = {
      label: target.label,
      entryPoint: `targets/${name}/api.d.ts`,
      tsconfig: `targets/${name}/tsconfig.json`,
      description: target.description,
    };
  }
  await writeJson(path.join(outputRoot, "reference.config.json"), { schemaVersion: 1, targets: publicTargets });
  console.log(`Scripting contract built at ${outputRoot}`);
};
