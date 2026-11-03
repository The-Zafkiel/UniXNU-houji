#!/usr/bin/env python3
"""Bounded Apple device-tree codec for local houji boot inputs."""
from dataclasses import dataclass, field
import struct


@dataclass
class Node:
    properties: list = field(default_factory=list)
    children: list = field(default_factory=list)

    def get(self, name):
        return next((value for key, flags, value in self.properties if key == name), None)

    @property
    def name(self):
        value = self.get('name')
        if not value or value[-1:] != b'\0':
            raise ValueError('ADT node has no terminated name')
        return value[:-1].decode('ascii')

    def set(self, name, value):
        matches = [i for i, item in enumerate(self.properties) if item[0] == name]
        if len(matches) > 1:
            raise ValueError('Duplicate ADT property')
        if matches:
            self.properties[matches[0]] = (name, 0, value)
        else:
            self.properties.append((name, 0, value))

    def child(self, name):
        matches = [item for item in self.children if item.name == name]
        if len(matches) != 1:
            raise ValueError(f'Expected exactly one ADT node: {name}')
        return matches[0]


def parse(data):
    if len(data) > 16 * 1024 * 1024:
        raise ValueError('ADT exceeds bound')
    nodes = 0

    def read(offset, depth):
        nonlocal nodes
        nodes += 1
        if depth > 64 or nodes > 4096 or offset + 8 > len(data):
            raise ValueError('Invalid ADT node bounds')
        props, children = struct.unpack_from('<II', data, offset)
        offset += 8
        if props > (len(data) - offset) // 36 or children > (len(data) - offset) // 8:
            raise ValueError('Invalid ADT counts')
        node = Node()
        seen = set()
        for _ in range(props):
            if offset + 36 > len(data):
                raise ValueError('Truncated ADT property')
            name, encoded = struct.unpack_from('<32sI', data, offset)
            if b'\0' not in name:
                raise ValueError('Unterminated ADT property name')
            name = name.split(b'\0', 1)[0].decode('ascii')
            length, flags = encoded & 0x7fffffff, encoded >> 31
            start = offset + 36
            offset = start + ((length + 3) & ~3)
            if offset > len(data) or name in seen:
                raise ValueError('Truncated or duplicate ADT property')
            seen.add(name)
            node.properties.append((name, flags, data[start:start + length]))
        for _ in range(children):
            child, offset = read(offset, depth + 1)
            node.children.append(child)
        return node, offset

    root, consumed = read(0, 0)
    if consumed != len(data):
        raise ValueError('Trailing ADT data')
    return root


def serialize(node):
    output = bytearray(struct.pack('<II', len(node.properties), len(node.children)))
    seen = set()
    for name, flags, value in node.properties:
        encoded = name.encode('ascii')
        if not encoded or len(encoded) > 31 or b'\0' in encoded or name in seen or flags not in (0, 1):
            raise ValueError('Invalid ADT property')
        seen.add(name)
        output += struct.pack('<32sI', encoded, len(value) | flags << 31)
        output += value + bytes(-len(value) % 4)
    for child in node.children:
        output += serialize(child)
    return bytes(output)
