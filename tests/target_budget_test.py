#!/usr/bin/env python3
"""Reject disassembly that splits FPU instructions into fake branches/calls."""
from pathlib import Path
import tempfile
import target_budget

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "float.dis"
    source.write_text("fm6_render:\n 1000: 3f e5 11 b6 \tr11 = r1 - r6 (f)\n")
    instructions = target_budget.functions(source)["fm6_render"]
    assert len(instructions) == 1
    assert target_budget.cost(instructions)["call"] == 0
    source.write_text("fm6_render:\n 1000: 3f e5 \t<unknown instruction>\n"
                      " 1002: 41 40 \tif (r1 == 0) goto -4 <fm6_render: 1000 >\n")
    try:
        target_budget.functions(source)
    except SystemExit as error:
        assert "-mattr=+fprev1" in str(error)
    else:
        raise AssertionError("undecoded FPU instruction accepted as an integer branch")
print("target budget: decoded FPU instructions counted once; undecoded instructions rejected")
