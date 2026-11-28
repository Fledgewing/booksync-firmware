#!/usr/bin/env python3
"""Compile every shared BookSync recorded case into a production C++ host test."""
import json
from pathlib import Path
import sys
rows=json.loads(Path(sys.argv[1]).read_text())
def opt(value):
    return 'std::nullopt' if value is None else str(value)
def boolean(value):
    return str(bool(value)).lower()
s='// Generated from the shared BookSync position fixture.\n'
for n,row in enumerate(rows['decide']):
    i=row['in']; marker=i.get('marker'); want=row['want']
    fields=[str(i.get('local',0)),opt(i.get('server')),str(i.get('tolerance',1)),str(i.get('server_at',0)),
        f'PositionMarker{{{marker["at"]},{marker["place"]}}}' if marker else 'std::nullopt',str(i.get('turn_at',0))]
    fields += [boolean(i.get(key)) for key in ('reset','redated','finished','released')]
    fields += [str(i.get('declined_at',0)),opt(i.get('override',{}).get('at')),boolean(i.get('percent_only'))]
    s+=f'TEST(SharedPosition, Decision{n+1}) {{ auto d=PositionPolicy::decide({{{",".join(fields)}}}); '
    s+=f'EXPECT_EQ(d.action, PositionAction::{want["decision"].capitalize()}); '
    s+=f'EXPECT_STREQ(d.rule,"{want["rule"]}"); EXPECT_EQ(d.dropped,{boolean(want.get("dropped"))}); }}\n'
for n,row in enumerate(rows['apply']):
    s+=f'TEST(SharedPosition, Apply{n+1}) {{ EXPECT_EQ(PositionPolicy::apply(PositionAction::{row["decision"].capitalize()},"{row["moment"]}"), PositionApply::{row["applied"].capitalize()}); }}\n'
for n,row in enumerate(rows['push']):
    s+=f'TEST(SharedPosition, Push{n+1}) {{ EXPECT_EQ(PositionPolicy::mayPush({opt(row["place"])},{opt(row["server"])},{row.get("tolerance",1)},{boolean(row.get("exempt"))}),{boolean(row["allowed"])}); }}\n'
Path(sys.argv[2]).write_text(s)
