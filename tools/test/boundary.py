"""BookSync C/C++ source boundaries. All preprocessor branches are inspected."""
from pathlib import Path
import json
import gzip
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = ('src', 'lib', 'include', 'tailnet', 'web', 'scripts')
BUILD_INPUTS = ('platformio.ini', 'partitions.csv', '.gitmodules')
TEXT = {'.h', '.hpp', '.hh', '.c', '.cc', '.cpp', '.inc', '.S', '.json', '.html', '.css', '.js', '.yaml', '.yml', '.xml', '.txt', '.manifest', '.py', '.csv', '.svg', '.ini', '.cfg'}
# Strings precede comments so URLs, raw literals and comment markers in strings survive.
TOKENS = re.compile(r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|`(?:\\.|[^`\\])*`|//[^\n]*|/\*.*?\*/', re.S)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.M)
STD = {'cstdint', 'cstddef', 'array', 'string_view', 'type_traits', 'limits'}
VENDOR = re.compile(r'bookorbit|orbismart|grimmory|booklore', re.I)
# Same protocol inventory as the BookSync adapter guard; never a line baseline.
FIELDS = ('readStatus', 'progressPercentage', 'smartScopeId', 'seriesName', 'seriesId',
          'seriesIndex', 'lastReadAt', 'addedAt', 'updatedAt', 'contentVersion', 'hasCover',
          'seriesSummary', 'libraryVersion', 'generatedAt', 'finishedThisYear', 'readingGoal',
          'thumbnailUrl', 'detailUrl', 'downloadUrl', 'userkey', 'server_url',
          'accessToken', 'refreshToken', 'accessTokenExpiresAt', 'refreshTokenExpiresAt',
          'clientKind', 'deviceLabel', 'readingSeconds', 'durationSeconds', 'sessionId',
          'dashboardConfig', 'hasNext', 'hasPrevious')
WIRE = re.compile(r'/api/v1|/?koreader/plugin\b|\bcatalog/(?:books|sections|dashboard)|\b(?:' +
                  '|'.join(FIELDS) + r')\b|smart[ _-]?scopes?|x-auth-user|x-auth-key|'
                  r'/koreader/(?:users|syncs|credentials)|/auth/(?:login|refresh|logout)|'
                  r'/opds(?:-users\b|[/?"\']|$)', re.I)
PRIVATE = re.compile(r'\b(?:\w*(?:Store|Client|Adapter|Jobs?|Scanner)|SETTINGS|APP_STATE|Storage|FsFile|HTTPClient|WiFiClient|SyncOwner|KOReaderSync\w*|ReadingSyncUpload|StatsUploadClient)\b')
# UI may declare classes/aliases, but may not invent an external entry point. Link
# checks additionally validate the compiled UI-only target's undefined symbols.
EXTERN_CALL = re.compile(r'\b(?:extern\s+)?(?:void|bool|int|auto|char|size_t)\s+\w+\s*\([^{};]*\)\s*;')


def code_only(source):
    return TOKENS.sub(lambda m: '\n' * m[0].count('\n') if m[0].startswith(('//', '/*')) else m[0], source.replace('\\\n', ''))


def normalized(source, strip_comments=True):
    source = (code_only(source) if strip_comments else source).replace('\\\n', '')
    def decode(m):
        token = m[0]
        if token.startswith('R"'):
            return '"' + token[token.index('(') + 1:token.rindex(')')] + '"'
        if token.startswith(('"', "'")):
            return re.sub(r'\\(?:x([0-9a-fA-F]+)|u([0-9a-fA-F]{4})|U([0-9a-fA-F]{8})|([0-7]{1,3}))',
                          lambda e: chr(int(next(g for g in e.groups() if g), 8 if e[4] else 16)), token)
        return token
    source = TOKENS.sub(decode, source)
    # Adjacent string literals may split a protocol name, including prefixed literals.
    return re.sub(r'"\s*(?:u8|u|U|L)?"', '', source)


def server_errors(name, source):
    if name.startswith(('src/booksync/server/bookorbit/', 'src/booksync/import/')):
        return []
    if Path(name).suffix in {'.json', '.yaml', '.yml', '.xml', '.html', '.txt', '.manifest', '.py', '.csv', '.svg', '.ini', '.cfg'}:
        # Resource prose is not C++: in particular an unquoted URL's // is
        # not a comment. XML comments and YAML comments have their own syntax.
        resource = re.sub(r'<!--.*?-->', '', source, flags=re.S)
        if Path(name).suffix in {'.yaml', '.yml', '.py', '.ini', '.cfg'}:
            resource = re.sub(r'''"(?:\\.|[^"\\])*"|'(?:''|[^'])*'|(?<!\S)#[^\n]*''',
                              lambda m: '' if m[0].startswith('#') else m[0], resource)
        code = normalized(resource, strip_comments=False)
    else:
        code = normalized(source)
    # Generated web headers contain gzip byte arrays, not source literals. Decode
    # each gzip payload so generated resources receive the same protocol check.
    if name.endswith('.generated.h'):
        for body in re.findall(r'\{([^{}]+)\}', source, re.S):
            octets = re.findall(r'0x([0-9a-fA-F]{2})\b', body)
            if octets:
                payload = bytes(int(x, 16) for x in octets)
                if payload.startswith(b'\x1f\x8b'):
                    try:
                        code += '\n' + gzip.decompress(payload).decode('utf-8', errors='replace')
                    except (OSError, EOFError):
                        return [f'{name}: invalid generated gzip resource']
    code = re.sub(r'\s*##\s*', '', code)
    errors = []
    if VENDOR.search(code) or VENDOR.search(name):
        errors.append(f'{name}: backend identifier or literal outside adapter/import')
    # Retained native sync remains operational until F05; it is not the product
    # adapter. Product code cannot call it or reuse its wire fields/routes.
    if name.startswith('src/booksync/') and WIRE.search(code):
        errors.append(f'{name}: wire protocol outside adapter/import')
    return errors


def ui_source_errors(name, source):
    errors = []
    restricted = name.startswith(('src/booksync/ui/', 'src/booksync/interface/', 'src/booksync/import/'))
    if not restricted:
        return errors
    code = code_only(source)
    if re.search(r'^\s*#\s*include\s+(?![<"])(?=\S)', code, re.M):
        errors.append(f'{name}: macro include cannot bypass public include closure')
    for inc in INCLUDE.findall(code):
        if name.startswith('src/booksync/import/'):
            if re.search(r'HTTP|TLS|WiFi|Transport|ServerAdapter|server/|tailnet/', inc, re.I):
                errors.append(f'{name}: importer transport dependency {inc}')
        elif inc not in STD and not (inc.startswith('booksync/interface/') or '/' not in inc and inc in {'Library.h', 'Models.h', 'Commands.h', 'Capabilities.h', 'Delivery.h'}):
            errors.append(f'{name}: private/unapproved include {inc}')
    if name.startswith('src/booksync/interface/'):
        bare = TOKENS.sub(' ', code)
        if re.search(r'\b(?:class|struct)\s+\w+\s*;', bare) or re.search(r'[&*]', bare):
            errors.append(f'{name}: public values cannot expose opaque pointers/references or forward handles')
    if name.startswith(('src/booksync/ui/', 'src/booksync/interface/')):
        identifiers = TOKENS.sub(lambda m: ' ' if m[0].startswith(('"', "'", 'R"')) else m[0], code)
        if PRIVATE.search(identifiers) or (name.startswith('src/booksync/ui/') and EXTERN_CALL.search(identifiers)):
            errors.append(f'{name}: private symbol or undeclared external service')
    return errors


def files(root):
    inputs = {p.relative_to(root).as_posix() for directory in PRODUCTION
              for p in (root / directory).rglob('*') if p.is_file() and not any(part in {'.git', '__pycache__'} for part in p.relative_to(root).parts)}
    inputs.update(name for name in BUILD_INPUTS if (root / name).is_file())
    config = root / 'platformio.ini'
    if config.is_file():
        for script in re.findall(r'^\s*(?:pre|post):([^\s]+\.py)\s*$', config.read_text(), re.M):
            # Dependency-owned hooks are covered by their immutable module pin;
            # project-owned hooks need their own inventory entry wherever placed.
            if not script.startswith(('.pio/', 'freeink-sdk/')):
                inputs.add(script)
    return inputs


def includes(source):
    return sorted(set(INCLUDE.findall(code_only(source))))


def private_product_symbols(root):
    public = set()
    private = set()
    for path in (root / 'src/booksync').rglob('*'):
        if not path.is_file() or path.suffix not in {'.h', '.cpp'}:
            continue
        code = TOKENS.sub(' ', code_only(path.read_text()))
        if path.is_relative_to(root / 'src/booksync/interface'):
            public.update(re.findall(r'\b\w+\b', code))
        else:
            private.update(re.findall(r'\b(?:class|struct)\s+(\w+)', code))
            private.update(re.findall(r'\b(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?[;{]', code))
    keywords = {'if', 'for', 'while', 'switch', 'catch', 'sizeof', 'alignof', 'decltype', 'static_assert', 'return', 'new', 'delete', 'noexcept'}
    return private - public - keywords


def ui_errors(root, inventory):
    actual = files(root)
    private = private_product_symbols(root)
    errors = [f'{p}: unclassified production input' for p in sorted(actual - inventory.keys())]
    errors += [f'{p}: stale inventory entry' for p in sorted(inventory.keys() - actual) if not inventory[p].get('generated_by')]
    for name in sorted(actual & inventory.keys()):
        role = inventory[name]
        if not role.get('rationale'):
            errors.append(f'{name}: missing dependency rationale')
        path = root / name
        if not path.is_file():
            errors.append(f'{name}: declared build input missing')
            continue
        if path.suffix not in TEXT:
            continue
        source = path.read_text(errors='replace')
        errors += ui_source_errors(name, source)
        if name.startswith(('src/booksync/ui/', 'src/booksync/interface/')):
            identifiers = set(re.findall(r'\b\w+\b', TOKENS.sub(' ', code_only(source))))
            for symbol in sorted(identifiers & private):
                errors.append(f'{name}: private product symbol {symbol}')
        if not name.startswith('src/booksync/'):
            if includes(source) != role.get('includes', []):
                errors.append(f'{name}: retained dependency edges changed; review inventory rationale')
        else:
            expected = name.split('/')[2]
            if role['role'] != expected:
                errors.append(f'{name}: role must match owning directory {expected}')
            if expected == 'interface' and name.endswith('ServerAdapter.h'):
                errors.append(f'{name}: adapter is worker-only, never a public UI header')
        # Retained files are inventoried, not a place to smuggle new product behavior.
        if not name.startswith('src/booksync/') and re.search(r'booksync/(?:services|storage|server|import|ui)', source):
            errors.append(f'{name}: product dependency must use a declared bridge')
    return errors


def load(root=ROOT):
    return json.loads((root / 'module-layers.json').read_text())


def check_server(root=ROOT):
    return [e for name in sorted(files(root))
            for e in server_errors(name, (root / name).read_text(errors='replace') if (root / name).is_file() and (root / name).suffix in TEXT else '')]


def check_ui(root=ROOT):
    manifest = load(root)
    errors = ui_errors(root, manifest['files'])
    for name, record in manifest['files'].items():
        if record.get('generated_by') and not (root / record['generated_by']).is_file():
            errors.append(f'{name}: missing declared generator')
    sdk = root / 'freeink-sdk'
    if not (sdk / 'library.json').exists() and not (sdk / '.git').exists():
        return errors + ['freeink-sdk: initialize the pinned submodule before checking']
    sha = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if sha != manifest['pinned']['freeink-sdk']:
        errors.append('freeink-sdk: inventory pin differs from checked-out SDK')
    gitlink = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD:freeink-sdk'], text=True).strip()
    if sha != gitlink:
        errors.append('freeink-sdk: inventory pin differs from parent gitlink')
    sdk_inputs = subprocess.check_output(['git', '-C', str(sdk), 'ls-files'], text=True).splitlines()
    if sdk_inputs != manifest['pinned']['freeink-sdk-inputs']:
        errors.append('freeink-sdk: pinned source/resource inventory changed')
    if subprocess.check_output(['git', '-C', str(sdk), 'status', '--porcelain', '--untracked-files=all'], text=True).strip():
        errors.append('freeink-sdk: modified/unclassified input in pinned third-party module')
    return errors
