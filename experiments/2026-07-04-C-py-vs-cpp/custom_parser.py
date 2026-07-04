from lab.parser import Parser


def get_parser() -> Parser:
    parser = Parser()
    # Translator's own final line, printed identically by both py and cpp:
    #   "Done! [<cpu>s CPU, <wall>s wall-clock]"
    parser.add_pattern(
        "translator_time", r"Done! \[(.+?)s CPU, .+?s wall-clock\]", type=float
    )
    parser.add_pattern(
        "translator_wall_time", r"Done! \[.+?s CPU, (.+?)s wall-clock\]", type=float
    )
    # Peak memory (max RSS) from /usr/bin/time -v: uniform for both translators.
    parser.add_pattern(
        "peak_memory_kb", r"Maximum resident set size \(kbytes\): (\d+)", type=int
    )
    # sha256 of output.sas -- the byte-equivalence key (py vs cpp per task).
    parser.add_pattern("sas_sha256", r"SAS_SHA256 ([0-9a-f]{64})", type=str)
    # Output-shape sanity (both translators print it).
    parser.add_pattern("translator_task_size", r"Translator task size: (\d+)", type=int)

    def set_coverage(content, props):
        # A task "translated" when it produced an output hash (Done! + file).
        props["coverage"] = 1 if "sas_sha256" in props else 0

    parser.add_function(set_coverage)
    return parser
