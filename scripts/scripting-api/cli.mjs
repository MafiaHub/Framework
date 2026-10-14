import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";

// Shared plumbing for the scripting-api tooling every project runs: argv parsing, JSON files, the
// output-root guard and the runner. A project's scripts/ entry points are thin wrappers that hand
// their own root to the commands next to this file.

export const option = (name) => {
  const index = process.argv.indexOf(name);
  return index === -1 ? undefined : process.argv[index + 1];
};

export const flag = (name) => process.argv.includes(name);

export const readJson = async (file) => JSON.parse(await readFile(file, "utf8"));

export const writeJson = (file, value) => writeFile(file, `${JSON.stringify(value, null, 2)}\n`, "utf8");

// A project is the directory holding scripting-api/; every path a command takes resolves from it.
export const projectPaths = (projectRoot) => {
  const repositoryRoot = path.resolve(projectRoot);
  return { repositoryRoot, apiRoot: path.join(repositoryRoot, "scripting-api") };
};

// Every command wipes its output first, so the output must never be able to contain the project.
export const resolveOutputRoot = (repositoryRoot, output, what) => {
  const outputRoot = path.resolve(repositoryRoot, output);
  if (outputRoot === path.parse(outputRoot).root || outputRoot === repositoryRoot || repositoryRoot.startsWith(`${outputRoot}${path.sep}`)) {
    throw new Error(`${what} output must not be a filesystem root, the repository root, or one of its parents`);
  }
  return outputRoot;
};

export const run = (main) => {
  Promise.resolve()
    .then(main)
    .catch((error) => {
      console.error(error instanceof Error ? error.message : error);
      process.exitCode = 1;
    });
};
