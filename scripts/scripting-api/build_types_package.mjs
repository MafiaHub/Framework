import { cp, mkdir, rm } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";
import { option, projectPaths, readJson, resolveOutputRoot, writeJson } from "./cli.mjs";

// Builds a project's types package (its scripting-api/package.json `name`), the committed
// declarations as an npm package, so a gamemode takes them from a registry instead of downloading
// files by hand.
//
// scripting-api/ is the package: its package.json is the manifest, and a gamemode in the
// repository links the directory as it stands. The published copy is that directory's `files`
// with the release version written in and the package's own README beside them, so what a release
// publishes is exactly what the repository compiled against.

/**
 * node scripts/build_types_package.mjs --out <directory> --version <semver>
 *
 * @param {object} options
 * @param {string} options.projectRoot directory holding scripting-api/
 */
export const buildTypesPackage = async ({ projectRoot }) => {
  const { repositoryRoot, apiRoot } = projectPaths(projectRoot);
  const output = option("--out");
  const version = option("--version");
  if (!output || !version) throw new Error("Usage: node scripts/build_types_package.mjs --out <directory> --version <semver>");
  if (!/^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$/.test(version)) throw new Error(`Invalid package version '${version}'`);
  const outputRoot = resolveOutputRoot(repositoryRoot, output, "Package");

  for (const side of ["server", "client"]) {
    const declaration = path.join(apiRoot, "generated", `${side}-api.d.ts`);
    if (!existsSync(declaration)) throw new Error(`${path.relative(repositoryRoot, declaration)} is missing; a package must carry both sides`);
  }

  const manifest = await readJson(path.join(apiRoot, "package.json"));
  await rm(outputRoot, { recursive: true, force: true });
  for (const file of manifest.files) {
    await mkdir(path.dirname(path.join(outputRoot, file)), { recursive: true });
    await cp(path.join(apiRoot, file), path.join(outputRoot, file));
  }
  await cp(path.join(apiRoot, "package", "README.md"), path.join(outputRoot, "README.md"));

  manifest.version = version;
  await writeJson(path.join(outputRoot, "package.json"), manifest);
  console.log(`${manifest.name}@${version} built at ${outputRoot}`);
};
