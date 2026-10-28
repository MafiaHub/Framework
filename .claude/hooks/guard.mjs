// PreToolUse hook: enforces the AGENTS.md rules an agent can be checked against mechanically.
// - File tools may only write inside the repository (non-negotiable rule 2). Temporary directories
//   and the agent's own ~/.claude state are allowed; the game install, home and profile data are not.
// - CMake may only drive the canonical build tree: `build/` on macOS/Linux, and on Windows nothing but
//   builds\build.bat (the mandatory Windows build rule).
import { existsSync, readFileSync, realpathSync } from "node:fs";
import os from "node:os";
import path from "node:path";

const input = JSON.parse(readFileSync(0, "utf8"));
const projectDir = path.resolve(process.env.CLAUDE_PROJECT_DIR || input.cwd);
const windows = process.platform === "win32";

const deny = (reason) => {
    process.stdout.write(JSON.stringify({ hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "deny", permissionDecisionReason: reason } }));
    process.exit(0);
};

// Resolves symlinks through the closest existing ancestor, so /tmp and /private/tmp compare equal.
const canonical = (target) => {
    let existing = path.resolve(target);
    const rest = [];
    while (!existsSync(existing) && path.dirname(existing) !== existing) {
        rest.unshift(path.basename(existing));
        existing = path.dirname(existing);
    }
    return path.join(realpathSync.native(existing), ...rest);
};

const within = (target, root) => {
    const relative = path.relative(canonical(root), canonical(target));
    return relative === "" || (!relative.startsWith("..") && !path.isAbsolute(relative));
};

const checkWrite = (file) => {
    const roots = [projectDir, os.tmpdir(), path.join(os.homedir(), ".claude"), ...(windows ? [] : ["/tmp"])];
    if (!roots.some((root) => within(file, root))) {
        deny(`AGENTS.md non-negotiable rule 2: never touch files outside the repository. ${file} is outside ${projectDir}. If the change has to happen there, tell the user and let them make it.`);
    }
};

// Splits a shell command into simple commands of unquoted words. Operators inside quotes stay part of
// the word, so `echo "cmake --build x"` is an echo, not a build.
const simpleCommands = (command) => {
    const commands = [[]];
    let word = null;
    let quote = null;
    const endWord = () => {
        if (word !== null) {
            commands.at(-1).push(word);
            word = null;
        }
    };
    for (let i = 0; i < command.length; i++) {
        const c = command[i];
        if (quote) {
            if (c === quote) {
                quote = null;
            } else if (c === "\\" && quote === '"' && i + 1 < command.length) {
                word += command[++i];
            } else {
                word += c;
            }
        } else if (c === "'" || c === '"') {
            quote = c;
            word ??= "";
        } else if (c === "\\" && i + 1 < command.length && !windows) {
            word = (word ?? "") + command[++i];
        } else if (/\s/.test(c) && c !== "\n") {
            endWord();
        } else if (/[;&|\n()]/.test(c)) {
            endWord();
            commands.push([]);
        } else {
            word = (word ?? "") + c;
        }
    }
    endWord();
    return commands;
};

// The program a simple command runs, past environment assignments and wrappers such as `mise exec --`.
const program = (words) => {
    let i = 0;
    while (i < words.length && /^\w+=/.test(words[i])) i++;
    if (["env", "time", "nice"].includes(words[i])) i++;
    if (words[i] === "mise" && ["exec", "x"].includes(words[i + 1])) {
        const separator = words.indexOf("--", i);
        i = separator === -1 ? words.length : separator + 1;
    }
    return { name: path.basename(words[i] ?? "").toLowerCase(), args: words.slice(i + 1) };
};

const checkCMake = (command) => {
    for (const words of simpleCommands(command)) {
        const { name, args: tokens } = program(words);
        if (name !== "cmake" && name !== "cmake.exe") {
            continue;
        }
        const directories = [];
        tokens.forEach((token, index) => {
            if (token === "--build" || token === "-B") {
                directories.push(tokens[index + 1] ?? "");
            } else if (token.startsWith("-B") && token.length > 2) {
                directories.push(token.slice(2));
            }
        });
        if (directories.length === 0) {
            continue;
        }
        if (windows) {
            deny("AGENTS.md mandatory Windows build rule: build through `builds\\build.bat <target> 64`, never `cmake --build` or `cmake -B` directly.");
        }
        const canonicalBuild = path.join(projectDir, "build");
        for (const directory of directories) {
            if (!directory || path.resolve(input.cwd, directory) !== canonicalBuild) {
                deny(`AGENTS.md build rules: use the canonical build tree \`build\` (run from the repository root, e.g. \`cmake --build build --target RunFrameworkTests\`), never an ad-hoc or temporary build directory. Got \`${directory}\`.`);
            }
        }
    }
};

const tool = input.tool_name;
if (tool === "Write" || tool === "Edit" || tool === "NotebookEdit") {
    checkWrite(input.tool_input?.file_path ?? input.tool_input?.notebook_path ?? "");
} else if (tool === "Bash" || tool === "PowerShell") {
    checkCMake(input.tool_input?.command ?? "");
}
