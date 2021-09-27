#! /usr/bin/env python3

import argparse
import logging
from pathlib import Path
import re
import subprocess
import sys

import markup

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT_DIR = SCRIPT_DIR.parents[1]

TXT2TAGS_OPTIONS = {
    "toc": False,
    "preproc": [
        [r"<<BR>>", "ESCAPED_LINEBREAK"],
    ],
    "postproc": [
        [r"ESCAPED_LINEBREAK", "<br />"],
        # Convert MoinMoin syntax for code to Markdown.
        [r"{{{", "```"],
        [r"}}}", "```"],
    ],
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--outdir", default="docs")
    parser.add_argument("--build", default="release")
    return parser.parse_args()


def build_planner(build):
    subprocess.check_call([sys.executable, "build.py", build, "downward"], cwd=REPO_ROOT_DIR)


def build_docs(build, outdir):
    out = subprocess.check_output(
        ["./fast-downward.py", "--build", build, "--search", "--", "--help", "--txt2tags"],
        cwd=REPO_ROOT_DIR).decode("utf-8")
    # Split the output into tuples (title, markup_text).
    pagesplitter = re.compile(r'>>>>CATEGORY: ([\w\s]+?)<<<<(.+?)>>>>CATEGORYEND<<<<', re.DOTALL)
    for title, markup_text in pagesplitter.findall(out) + [
            ("index", "Choose a plugin type on the left to see its documentation.")]:
        document = markup.Document(title="", date="")
        document.add_text(markup_text)
        output = document.render("md", options=TXT2TAGS_OPTIONS)
        print(document.text)
        with open(f"{outdir}/{title}.md", "w") as f:
            f.write(output)


if __name__ == '__main__':
    args = parse_args()
    logging.info("building planner...")
    build_planner(args.build)
    logging.info("building documentation...")
    outdir = SCRIPT_DIR / args.outdir
    try:
        outdir.mkdir()
    except FileExistsError as e:
        sys.exit(e)
    html = build_docs(args.build, outdir)
