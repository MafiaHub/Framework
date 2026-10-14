import path from "node:path";
import { flag, option, projectPaths, readJson, writeJson } from "./cli.mjs";

// Points a staged copy of a project's default gamemode at a released types package (the project's
// scripting-api/package.json `name`).
//
// The gamemode always compiles against the package, but in the repository the dependency is a
// file: reference to scripting-api/, so a binding and the gamemode code using it land in one
// commit. A release bundle carries no scripting-api/, so its copy depends on the release's exact
// version instead, and its lockfile is written to match: the shipped gamemode installs and builds
// with nothing fetched by hand.
//
// The lockfile entry is written here rather than resolved from the registry, because the release
// workflow publishes the package only once everything else has succeeded -- a failed release must
// not burn the version on npm. A registry package with no dependencies is, to npm, its version and
// the integrity of its tarball and nothing else, so the workflow packs the tarball first, passes
// its integrity in, and publishes that same file last.

// The tsconfig already names the package; a staged copy that does not is not one this rewrite
// would make buildable, so it fails the release rather than shipping.
const requireTypes = async (packageName, file, side) => {
  const tsconfig = await readJson(file);
  if (!tsconfig.compilerOptions?.types?.includes(`${packageName}/${side}`)) throw new Error(`${file} does not name ${packageName}/${side} in compilerOptions.types`);
};

// npm's lockfile is JSON: the root importer takes the version, and the linked package becomes a
// registry entry with the packed tarball's integrity in place of the scripting-api/ link.
const writeLockfile = async (packageName, file, version, integrity, sourceLink) => {
  const lockfile = await readJson(file);
  const packages = lockfile.packages ?? {};
  const linkTarget = sourceLink.slice("file:".length);
  const entry = `node_modules/${packageName}`;
  if (packages[""]?.devDependencies?.[packageName] !== sourceLink || !packages[entry]?.link || !packages[linkTarget]) {
    throw new Error(`The resource lockfile has no ${sourceLink} entry for ${packageName}; refresh it with npm and update the script`);
  }
  packages[""].devDependencies[packageName] = version;
  delete packages[linkTarget];
  packages[entry] = {
    version,
    // npm names a scoped package's tarball after the unscoped part: @scope/types -> types-<version>.tgz.
    resolved: `https://registry.npmjs.org/${packageName}/-/${packageName.split("/").pop()}-${version}.tgz`,
    integrity,
    dev: true,
  };
  await writeJson(file, lockfile);
};

/**
 * node scripts/stage_gamemode_types.mjs --gamemode <staged gamemode directory> --version <semver> (--integrity <sha512-...> | --skip-lockfile)
 *
 * @param {object} options
 * @param {string} options.projectRoot directory holding scripting-api/
 * @param {Record<string, "server" | "client">} options.tsconfigs each tsconfig of the gamemode, relative to its root, and the side it compiles against
 */
export const stageGamemodeTypes = async ({ projectRoot, tsconfigs }) => {
  const { apiRoot } = projectPaths(projectRoot);
  const packageName = (await readJson(path.join(apiRoot, "package.json"))).name;
  const gamemode = option("--gamemode");
  const version = option("--version");
  const integrity = option("--integrity");
  const skipLockfile = flag("--skip-lockfile");
  if (!gamemode || !version || (!skipLockfile && !integrity)) {
    throw new Error("Usage: node scripts/stage_gamemode_types.mjs --gamemode <staged gamemode directory> --version <semver> (--integrity <sha512-...> | --skip-lockfile)");
  }
  if (!skipLockfile && !/^sha512-[A-Za-z0-9+/]+={0,2}$/.test(integrity)) throw new Error(`'${integrity}' is not a sha512 integrity`);

  const root = path.resolve(gamemode);
  const manifestPath = path.join(root, "package.json");
  const manifest = await readJson(manifestPath);
  if (!manifest.devDependencies?.[packageName]?.startsWith("file:")) throw new Error(`${manifestPath} should depend on ${packageName} through a file: to scripting-api`);
  const sourceLink = manifest.devDependencies[packageName];
  manifest.devDependencies[packageName] = version;
  await writeJson(manifestPath, manifest);

  for (const [tsconfig, side] of Object.entries(tsconfigs)) {
    await requireTypes(packageName, path.join(root, tsconfig), side);
  }

  if (!skipLockfile) {
    await writeLockfile(packageName, path.join(root, "package-lock.json"), version, integrity, sourceLink);
  }
  console.log(`${path.basename(root)} now takes its declarations from ${packageName}@${version}`);
};
