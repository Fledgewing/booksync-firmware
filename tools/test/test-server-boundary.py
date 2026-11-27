#!/usr/bin/env python3
"""Check backend identifiers/literals in every production input, even disabled code."""
import sys
from boundary import check_server

errors = check_server()
print('\n'.join(errors) if errors else 'BookSync server boundary: PASS')
sys.exit(bool(errors))
