#!/usr/bin/env python3
"""Writes a long Lox script that reads and assigns globals over and over. Lox has
no loops yet, so the repetition has to be spelled out.

usage: gen_bench.py <globals> <statements> > bench.lox
"""
import sys

count = int(sys.argv[1])
statements = int(sys.argv[2])

for i in range(count):
    print(f'var g{i} = {i + 2};')
for i in range(statements):
    print(f'g{i % count} = g{(i + 1) % count} + 1;')
print('print g0;')
