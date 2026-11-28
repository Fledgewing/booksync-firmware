"""BookSync build identity: consume shared inputs, never own a release clock."""
import json
import re

PRODUCT = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(preview|rc|0\.test)\.([1-9][0-9]*))?(?:\+[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*)?')
STEMS = {'default': 'firmware-x3-x4', 'x4-pro': 'firmware-x4-pro', 'x4-classic': 'firmware-x4-classic'}
BASE = '2.0.0'
BASE_COMMIT = '2cdd6117568c8877c3180d5fac8d5b20e55d1ac7'


def valid_product(value):
    m = PRODUCT.fullmatch(value)
    return bool(m and all(int(x) <= 4294967295 for x in (*m.group(1, 2, 3), m[5] or '0')))


def identity(manifest=None, tag='', test_run=1):
    # Ordinary PR/local artifacts are explicitly nonpublishable. A real build
    # must receive the integration repo's shared/version.json as an input.
    product = json.loads(manifest.read_text())['version'] if manifest else '0.0.0'
    if not isinstance(product, str) or not valid_product(product):
        raise ValueError('invalid shared BookSync identity')
    if tag:
        if not manifest or '-0.test.' in product or '+' in product:
            raise ValueError('release requires shared non-test identity')
        unified = f'xteink-v{product}+crossink.{BASE}'
        core = tuple(map(int, PRODUCT.fullmatch(product).group(1, 2, 3)))
        if tag == unified and core < (0, 12, 0):
            raise ValueError('pre-unified versions require a legacy bridge tag')
        # Legacy bridge tags retain their numeric counter and exact metadata.
        bridge = re.fullmatch(r'xteink-v[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+\+[A-Za-z0-9.-]+', tag)
        if tag != unified and not bridge:
            raise ValueError('tag does not match shared version and pinned base')
        if bridge and core >= (0, 12, 0):
            raise ValueError('unified versions require unified tags')
        return product, tag
    if not re.fullmatch(r'[1-9][0-9]*', str(test_run)) or int(test_run) > 4294967295:
        raise ValueError('invalid test run')
    return '.'.join(PRODUCT.fullmatch(product).group(1, 2, 3)) + f'-0.test.{test_run}', ''


def artifact_name(environment, tag):
    if not tag.startswith('xteink-v') or not re.fullmatch(r'[A-Za-z0-9.+-]+', tag):
        raise ValueError('invalid release tag')
    # Match the integration producer's existing asset_version normalization.
    # The immutable release/signature tag above still retains its exact '+'.
    asset_version = re.sub(r'[^A-Za-z0-9._-]+', '-', tag[len('xteink-v'):])
    return STEMS[environment] + '-v' + asset_version + '.bin'
