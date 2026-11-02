#!/usr/bin/env python3
"""Which phase of the fight program (BOOT, LOADING, FIGHT, TRANSITION) uses each function and each RAM symbol.

Static, from the linked ELF and main.c. Roots are the identifiers main.c uses in each phase's source lines (a static
function of main.c is followed into its body); the closure runs over the binary: direct calls and branches, pool
constants followed by a jsr/jmp through the same register (calls), and every other function address taken in code,
rodata or data (callbacks, tables, vectors) is a FIGHT root. A symbol is used by a phase when a function of that phase
has its address (or an address inside it) in a pool constant. `main` itself holds all phases, so the symbols only it
touches are reported as 'main' and need the source.

  python3 scripts/memory_phases.py build/tekken3_saturn.elf src/saturn/main.c [--min 1024] [--json out.json]
"""
import argparse
import collections
import json
import re
import subprocess
from pathlib import Path

TOOLS = '/Users/celso/dev/saturn-tools/bin/sh2eb-elf-'
PHASES = ('BOOT', 'LOADING', 'FIGHT', 'TRANSITION')
FUNC = re.compile(r'^([0-9a-f]{8}) <(\S+)>:$')
INSN = re.compile(r'^\s*([0-9a-f]+):\t(\S+)\s*(.*)$')
POOL = re.compile(r'^\S+\s+[0-9a-f]+ <[^>]*>,(r\d+)\s+! ([0-9a-f]+)$')
POOL_PLAIN = re.compile(r'^[0-9a-f]+\s*(?:<[^>]*>)?,(r\d+)\s+! ([0-9a-f]+)$')
BRANCH = re.compile(r'^([0-9a-f]+) <([^>+]+)>$')   # a branch to a function's first byte: a call or a tail call


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def symbols(elf):
    out = []
    for line in run(TOOLS + 'nm', '-n', '-S', str(elf)).splitlines():
        parts = line.split()
        if len(parts) == 4:
            out.append((int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]))
    return out


def disassemble(elf):
    functions = collections.OrderedDict()
    current = None
    for line in run(TOOLS + 'objdump', '-d', '--no-show-raw-insn', str(elf)).splitlines():
        match = FUNC.match(line)
        if match:
            current = match[2]
            functions[current] = []
            continue
        match = INSN.match(line)
        if match and current:
            functions[current].append((int(match[1], 16), match[2], match[3].split('!')[0].strip(), match[3]))
    return functions


def data_words(elf, skip, addresses):
    """Words of every section but `skip` (code) that equal a function address."""
    found = set()
    active = False
    for line in run(TOOLS + 'objdump', '-s', str(elf)).splitlines():
        if line.startswith('Contents of section'):
            active = line.split()[-1].rstrip(':') not in skip
            continue
        parts = line.split()
        if not active or len(parts) < 2 or not re.fullmatch(r'[0-9a-f]+', parts[0]):
            continue
        for word in parts[1:5]:
            if re.fullmatch(r'[0-9a-f]{8}', word) and int(word, 16) in addresses:
                found.add(int(word, 16))
    return found


def source_functions(lines):
    bodies = {}
    start = None
    for number, text in enumerate(lines, 1):
        match = re.match(r'^(?:static\s+)?[\w\s\*]+?\b(\w+)\([^;]*\)\s*\{\s*$', text)
        if match and not text.startswith((' ', '\t')) and start is None:
            start, name = number, match[1]
        elif text.startswith('}') and start is not None:
            bodies[name] = (start, number)
            start = None
    return bodies


def header_bodies(roots):
    """Functions defined in headers (static inline): name -> identifiers of the body."""
    bodies = {}
    for root in roots:
        for header in Path(root).rglob('*.h'):
            text = header.read_text(errors='ignore').splitlines()
            for name, (first, last) in source_functions(text).items():
                bodies.setdefault(name, set()).update(identifiers(text, first, last))
    return bodies


def identifiers(lines, first, last):
    names = set()
    for text in lines[first - 1:last]:
        if text.lstrip().startswith(('*', '/*', '//')):
            continue
        text = re.sub(r'//.*|/\*.*?\*/', '', text)
        names.update(re.findall(r'[A-Za-z_]\w*', text))
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('elf', type=Path)
    parser.add_argument('main_c', type=Path)
    parser.add_argument('--min', type=int, default=1024, help='smallest RAM symbol listed')
    parser.add_argument('--json', type=Path)
    parser.add_argument('--headers', nargs='*', default=['src', '/Users/celso/dev/saturn-prefix/include'],
                        help='directories whose header functions the source roots follow into')
    parser.add_argument('--why', nargs='*', help='functions to explain: the shortest path from a FIGHT root')
    args = parser.parse_args()

    lines = args.main_c.read_text().splitlines()
    inline = header_bodies(args.headers)
    bodies = source_functions(lines)
    entry = bodies['main']
    loop_start = next(n for n in range(entry[0], entry[1]) if re.match(r'\s*for\s*\(\s*;;\s*\)\s*\{', lines[n - 1]))
    loading_start = next(n for n in range(entry[0], entry[1]) if 'enter_fight_phase();' in lines[n - 1])
    spans = {'BOOT': [(entry[0], loading_start - 1)], 'LOADING': [(loading_start, loop_start - 1)],
             'FIGHT': [(loop_start, entry[1])], 'TRANSITION': []}

    def follow(phase):
        names = set()
        todo = list(spans[phase])
        done = set()
        pending = []
        while todo or pending:
            if pending:
                for name in [n for n in pending if n in inline and n not in done]:
                    done.add(name)
                    pending.extend(inline[name])
                names.update(pending)
                pending = []
                continue
            first, last = todo.pop()
            for name in identifiers(lines, first, last):
                names.add(name)
                if name in bodies and name not in done and name != 'main':
                    done.add(name)
                    todo.append(bodies[name])
                if name in inline and name not in done:
                    done.add(name)
                    names.update(inline[name])
                    pending.extend(inline[name])
        return names

    # restart_program is the transition (sat_exec); the fight loop only calls it.
    spans['TRANSITION'] = [bodies['restart_program']]
    roots_source = {phase: follow(phase) for phase in PHASES}
    roots_source['FIGHT'] -= follow('TRANSITION') - identifiers(lines, loop_start, entry[1]) if False else set()

    syms = symbols(args.elf)
    functions = disassemble(args.elf)
    func_addr = {}
    for address, size, kind, name in syms:
        if name in functions:
            func_addr[address] = name
    by_name = {name: address for address, name in func_addr.items()}
    ranges = sorted((address, address + size, name) for address, size, kind, name in syms if kind.lower() in 'bdr')

    edges = collections.defaultdict(set)      # caller -> callees (calls and branches)
    taken = set()                             # functions whose address is taken
    refs = collections.defaultdict(set)       # data symbol -> functions that load its address
    import bisect
    starts = [r[0] for r in ranges]
    for name, code in functions.items():
        for index, (address, op, operands, raw) in enumerate(code):
            match = BRANCH.match(operands)
            if op in ('bsr', 'bra') and match:
                target = match[2]
                if target in functions and target != name:
                    edges[name].add(target)
                continue
            pool = re.match(r'^[^,]*,(r\d+)$', operands)
            comment = re.search(r'!\s*([0-9a-f]+)(?:\s+<[^>]*>)?\s*$', raw)
            if op == 'mov.l' and pool and comment:
                value = int(comment[1], 16)
                register = pool[1]
                if value in func_addr and func_addr[value] != name:
                    called = False
                    for later in code[index + 1:index + 40]:
                        if later[1] in ('jsr', 'jmp') and later[2] == '@' + register:
                            called = True
                            break
                        if re.search(r',' + register + r'$', later[2]) and later[1] != 'cmp/eq':
                            break
                    (edges[name].add(func_addr[value]) if called else taken.add(func_addr[value]))
                    if not called:
                        edges[name].add(func_addr[value])
                position = bisect.bisect_right(starts, value) - 1
                if position >= 0 and ranges[position][0] <= value < ranges[position][1]:
                    refs[ranges[position][2]].add(name)
    taken |= {func_addr[a] for a in data_words(args.elf, ('.text',), func_addr)}

    def closure(roots):
        seen = set()
        todo = [r for r in roots if r in functions]
        while todo:
            node = todo.pop()
            if node in seen:
                continue
            seen.add(node)
            todo.extend(edges[node] - seen)
        return seen

    called = {callee for callees in edges.values() for callee in callees}
    named = {'_' + n for phase in ('BOOT', 'LOADING', 'TRANSITION') for n in roots_source[phase]}
    # A function nothing calls and no phase names is an interrupt vector or an entry point: treat it as FIGHT.
    taken |= {name for name in functions if name not in called and name not in named and name not in ('_main', '_start')}
    edges['_main'] = set()      # main holds every phase: its source lines are the roots, not its binary calls

    def why(target, roots):
        parent = {r: None for r in roots if r in functions}
        queue = list(parent)
        while queue:
            node = queue.pop(0)
            if node == target:
                chain = []
                while node is not None:
                    chain.append(node)
                    node = parent[node]
                return chain[::-1]
            for nxt in edges[node]:
                if nxt not in parent:
                    parent[nxt] = node
                    queue.append(nxt)
        return None

    if args.why:
        fight_roots = {'_' + n for n in roots_source['FIGHT']} | taken
        for target in args.why:
            print('FIGHT path to %s: %s' % (target, ' > '.join(why(target, fight_roots) or ['none'])))
    reach = {}
    for phase in PHASES:
        roots = {'_' + n for n in roots_source[phase]}
        if phase == 'FIGHT':
            roots |= taken
        reach[phase] = closure(roots)
    # the loop is entered from LOADING; everything FIGHT reaches is FIGHT, and a transition callee can also be FIGHT.
    sizes = {name: size for address, size, kind, name in syms if name in functions}
    uses = {name: tuple(p for p in PHASES if name in reach[p]) for name in functions}

    print('functions: %d, address-taken: %d' % (len(functions), len(taken)))
    total = collections.Counter()
    for name, phases in uses.items():
        total[phases] += sizes.get(name, 0)
    for phases, size in sorted(total.items(), key=lambda item: -item[1]):
        print('%8d B code  %s' % (size, '+'.join(phases) or 'unreached by the roots'))
    only = sorted(((sizes.get(n, 0), n) for n, p in uses.items() if p and 'FIGHT' not in p and 'TRANSITION' not in p),
                  reverse=True)
    print('\ncode no FIGHT root reaches (LOADING/BOOT only): %d B in %d functions' %
          (sum(s for s, _ in only), len(only)))
    for size, name in only[:25]:
        print('  %6d %s %s' % (size, name, '+'.join(uses[name])))

    print('\nRAM symbols >= %d B by phase of the functions that load their address:' % args.min)
    rows = []
    for address, size, kind, name in syms:
        if kind.lower() not in 'bdr' or size < args.min:
            continue
        users = refs.get(name, set())
        phases = set()
        for user in users:
            if user == '_main':
                phases.add('main')
            phases.update(uses.get(user, ()) or ('unreached',))
        rows.append((size, name, hex(address), kind, sorted(phases), len(users)))
    for size, name, address, kind, phases, count in sorted(rows, reverse=True):
        print('%8d %s %s %s [%s] %d users' % (size, address, kind, name, '+'.join(phases), count))
    if args.json:
        args.json.write_text(json.dumps({'ram': rows, 'code': {n: list(p) for n, p in uses.items()},
                                         'sizes': sizes}, indent=1))


if __name__ == '__main__':
    main()
