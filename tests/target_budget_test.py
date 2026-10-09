#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assembly layouts must not turn alternative paths into nested DSP loops."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('target_budget', Path(__file__).with_name('target_budget.py'))
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


def listing(*lines):
    return [(i * 4, text) for i, text in enumerate(lines)]


# A branch body is placed after the merge. Its backwards jump executes once.
alternative = listing('if (r0 == 0) goto 16 <body : 10 >', 'r1 = 1',
                      'goto 4 <merge : c >', '{pc, r4} = [sp++]', 'r1 = 2',
                      'goto -8 <merge : c >', '{pc, r4} = [sp++]')
assert budget.loop_members(alternative) == []
assert budget.cost(alternative)['cost'] == 0

# Two genuine nested loops retain the existing nesting weights and divides.
nested = listing('r0 = 0', 'r1 = 0', 'r2 = r3 / r4',
                 'if (r1 < 8) goto -4 <inner : 8 >',
                 'if (r0 < 8) goto -12 <outer : 4 >', '{pc, r4} = [sp++]')
assert {frozenset(loop) for loop in budget.loop_members(nested)} == {frozenset({1,2,3,4}), frozenset({2,3})}
assert budget.cost(nested)['cost'] == 42
assert budget.cost(nested)['div'] == 1

# A hot call remains visible; extracting FM6 output/join cannot hide work.
called = listing('r0 = 0', 'call 64 <helper : 44 >',
                 'if (r0 < 8) goto -4 <loop : 4 >', '{pc, r4} = [sp++]')
assert budget.cost(called)['call'] == 1
assert 'fm6_output' in budget.EXTRACTED['fm6_render']
assert 'fm6_join' in budget.EXTRACTED['fm6_render']

# Missing jump-table edges must keep the conservative old calculation.
indirect = alternative + [(28, 'goto r0')]
assert budget.cost(indirect)['cost'] > 0
# A genuine loop may straddle its latch in the compiler's physical layout.
split = listing('goto 8 <test : 8 >', 'r0 += 1',
                'if (r0 < 8) goto 8 <body : 10 >', '{pc, r4} = [sp++]',
                'r1 += 1', 'goto -16 <increment : 4 >')
assert budget.cost(split)['loop'] == 4
irreducible = listing('if (r0) goto 8 <b : 8 >', 'r1 += 1',
                      'if (r1) goto -4 <a : 4 >', '{pc, r4} = [sp++]')
assert budget.cost(irreducible)['cost'] > 0
unlisted = [(0, 'r0 = 0'), (8, 'r1 = 0'), (12, 'goto -2 <unlisted : a >')]
assert budget.cost(unlisted)['cost'] == 1
print('target budget: alternatives, split/nested loops, divides, calls and conservative fallbacks passed')
