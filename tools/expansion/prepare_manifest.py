"""Developer-only: bind the authored expansion edits to a verified translation.

Only addresses, hashes, edit counts and authored replacements are distributed.
Game function bodies stay in the player's locally built DLL.
"""
import hashlib
import json
import re
import sys
from pathlib import Path


def span(body, at):
    lines = body.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if line.strip().startswith('// ' + at + ' '):
            j = i + 1
            while j < len(lines) and not lines[j].strip().startswith('// ') and not re.match(r'loc_\w+:', lines[j]) and lines[j].strip() != '}':
                j += 1
            return sum(map(len, lines[:i+1])), sum(map(len, lines[:j]))
    raise ValueError(at)


def main():
    generated, source, output = map(Path, sys.argv[1:])
    funcs = {}
    for path in sorted(generated.glob('x86_recomp.*.cpp')):
        text = path.read_text()
        if re.search(r'^X86_FUNC\(sub_[0-9A-F]{8}_exp\)', text, re.M):
            raise ValueError('Manifest input is already patched; regenerate the untouched translation first: ' + str(path))
        for match in re.finditer(r'^X86_FUNC\(__imp__sub_([0-9A-F]{8})\) \{$', text, re.M):
            end = text.index('\n}\n', match.start()) + 3
            funcs[match[1]] = text[match.start():end]
    original = funcs.copy()
    result = {'format': 1, 'includes': ['game/expansion.h'], 'condition': 'expansion::Active()', 'edits': []}
    for edit in json.loads(source.read_text())['edits']:
        at = f"{int(edit['at'], 16):08X}"
        owners = [n for n, body in original.items() if '// ' + at + ' ' in body]
        if 'funcs' in edit:
            allowed = {f'{int(n,16):08X}' for n in edit['funcs']}
            owners = [n for n in owners if n in allowed]
        if not owners:
            raise ValueError('Missing instruction ' + at)
        entry = dict(edit, at=at, restricted='funcs' in edit, functions=[])
        entry.pop('funcs', None)
        for name in sorted(owners):
            a, b = span(original[name], at)
            expected = original[name] if edit.get('all') else original[name][a:b]
            body = funcs[name]
            a, b = span(body, at)
            if 'replacement' in edit:
                authored = '\t' + edit['replacement'] + '\n'
                if 'guard' in edit:
                    if not isinstance(edit['guard'], str) or not edit['guard']:
                        raise ValueError('Invalid instruction guard')
                    authored = '\tif (' + edit['guard'] + ') {\n' + authored + '\t} else {\n' + body[a:b] + '\t}\n'
                body = body[:a] + authored + body[b:]
                count = 1
            elif 'before' in edit:
                count = 1
                body = body[:a] + '\t' + edit['before'] + '\n' + body[a:]
            elif 'after' in edit:
                count = 1
                body = body[:b] + '\t' + edit['after'] + '\n' + body[b:]
            elif edit.get('all'):
                body, count = re.subn(re.escape(edit['old']) + r'(?![0-9A-Za-z_])', lambda _: edit['new'], body)
            else:
                lines = body[a:b].splitlines(keepends=True)
                for i, line in enumerate(lines):
                    if edit['old'] in line:
                        count = line.count(edit['old'])
                        lines[i] = line.replace(edit['old'], edit['new'])
                        break
                else:
                    raise ValueError('Literal mismatch ' + at)
                body = body[:a] + ''.join(lines) + body[b:]
            if not count:
                raise ValueError('No changes ' + at)
            funcs[name] = body
            entry['functions'].append({'function': name, 'translation_sha256': hashlib.sha256(expected.encode()).hexdigest(), 'occurrences': count})
        result['edits'].append(entry)
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{len(result['edits'])} validated edits, {sum(original[n] != funcs[n] for n in funcs)} function copies; no game bodies in manifest")


if __name__ == '__main__':
    main()
