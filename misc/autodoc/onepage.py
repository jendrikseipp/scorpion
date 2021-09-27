#! /usr/bin/env python3

import argparse
import logging
import os
from os.path import dirname, join
import re
import subprocess
import sys

import markup

DOC_PREFIX = "Doc/"

# a list of characters allowed to be used in doc titles
TITLE_WHITE_LIST = r"[\w\+-]" # match 'word characters' (including '_'), '+', and '-'

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
REPO_ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))

TXT2TAGS_OPTIONS = {
    "toc": True,
    "preproc": [
        [r"<<BR>>", "ESCAPED_LINEBREAK"],
    ],
    "postproc": [
        [r"</head>", """
<link rel="stylesheet" href="normalize.css" type="text/css" />
<link rel="stylesheet" href="style.css" type="text/css" />
<link rel="stylesheet" href="learn.css" type="text/css" />

</head>"""],
        [r"ESCAPED_LINEBREAK", "<br />"],
    ],
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("outfile")
    parser.add_argument("--build", default="release")
    return parser.parse_args()


def insert_wiki_links(text, titles):
    def make_link(m, prefix=''):
        anchor = m.group('anchor') or ''
        link_name = m.group('link')
        target = prefix + link_name
        if anchor:
            target += '#' + anchor
            link_name = anchor
        link_name = link_name.replace("_", " ")
        # Leave out the prefix in the link name.
        result = m.group('before') + "[[" + target + "|" + link_name + "]]" + m.group('after')
        return result

    def make_doc_link(m):
        return make_link(m, prefix=DOC_PREFIX)

    re_link = r"(?P<before>\W)(?P<link>%s)(#(?P<anchor>" + TITLE_WHITE_LIST + r"+))?(?P<after>\W)"
    doctitles = [title[4:] for title in titles if title.startswith(DOC_PREFIX)]
    for key in doctitles:
        text = re.sub(re_link % key, make_doc_link, text)
    othertitles = [title for title in titles
                   if not title.startswith(DOC_PREFIX) and title not in doctitles]
    for key in othertitles:
        text = re.sub(re_link % key, make_link, text)
    return text


def build_planner(build):
    subprocess.check_call([sys.executable, "build.py", build, "downward"], cwd=REPO_ROOT_DIR)


def build_docs(build):
    out = subprocess.check_output(
        ["./fast-downward.py", "--build", build, "--search", "--", "--help", "--txt2tags"],
        cwd=REPO_ROOT_DIR).decode("utf-8")
    # Split the output into tuples (title, markup_text).
    pagesplitter = re.compile(r'>>>>CATEGORY: ([\w\s]+?)<<<<(.+?)>>>>CATEGORYEND<<<<', re.DOTALL)
    document = markup.Document(title="Plugin reference", date="")
    for title, markup_text in pagesplitter.findall(out):
        document.add_text(f"= {title} =")
        document.add_text(markup_text)
    print(document.text)
    return document.render("xhtml", options=TXT2TAGS_OPTIONS)


if __name__ == '__main__':
    args = parse_args()
    logging.info("building planner...")
    build_planner(args.build)
    logging.info("getting new pages from planner...")
    html = build_docs(args.build)
    with open(args.outfile, "w") as f:
        f.write(html)
