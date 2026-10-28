// PostToolUse hook: formats the lines an agent just changed in a C/C++ file under code/framework/, the
// scope scripts/format_codebase.sh formats.
// Only changed lines, so an edit does not drag a whole file's existing formatting drift into the diff.
// Uses the clang-format pinned in mise.toml; skips with a note when it is not on PATH.
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import path from "node:path";

const SOURCE = /\.(c|cc|cpp|h|hpp)$/i;

const input = JSON.parse(readFileSync(0, "utf8"));
const projectDir = path.resolve(process.env.CLAUDE_PROJECT_DIR || input.cwd);
const file = path.resolve(input.tool_input?.file_path ?? "");
const relative = path.relative(projectDir, file).split(path.sep).join("/");

if (!SOURCE.test(file) || !relative.startsWith("code/framework/")) {
    process.exit(0);
}

const run = (command, args) => execFileSync(command, args, { cwd: projectDir, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });

const note = (text) => {
    process.stdout.write(JSON.stringify({ hookSpecificOutput: { hookEventName: "PostToolUse", additionalContext: text } }));
    process.exit(0);
};

// Ranges of changed lines in the working tree, from a zero-context diff against HEAD.
const changedRanges = () => {
    const ranges = [];
    for (const match of run("git", ["diff", "-U0", "--no-color", "HEAD", "--", relative]).matchAll(/^@@ -\S+ \+(\d+)(?:,(\d+))? @@/gm)) {
        const start = Number(match[1]);
        const count = match[2] === undefined ? 1 : Number(match[2]);
        if (count > 0) {
            ranges.push(`${start}:${start + count - 1}`);
        }
    }
    return ranges;
};

let tracked = true;
try {
    run("git", ["ls-files", "--error-unmatch", "--", relative]);
} catch {
    tracked = false;
}

// A new file has no drift to preserve, so it is formatted whole.
const ranges = tracked ? changedRanges() : null;
if (ranges !== null && ranges.length === 0) {
    process.exit(0);
}

try {
    run("clang-format", ["-i", ...(ranges ?? []).map((range) => `--lines=${range}`), relative]);
} catch (error) {
    if (error.code === "ENOENT") {
        note("clang-format is not on PATH, so this edit was not formatted. Run `mise install` in the repository.");
    }
    note(`clang-format failed on ${relative}: ${error.stderr || error.message}`);
}
