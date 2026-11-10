#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""(Beat It 1.5) Every fixed message the firmware shows in the message bar must fit ui.msg (ui.c: char msg[24],
so 23 characters): a longer one is cut off on the screen. Checks the string literals passed to ui_message() and
ui_say(), and #define ..._MSG strings. ui_say() joins two parts: both literals together must fit.
Exit status: the number of messages that are too long."""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "src")


def msg_size():
    src = open(os.path.join(ROOT, "ui.c")).read()
    m = re.search(r"char\s+msg\[(\d+)\]", src)
    return int(m.group(1)) - 1


def calls(src, name):
    """the argument text of each name(...) call, balanced parentheses, strings respected"""
    for m in re.finditer(r"\b%s\s*\(" % name, src):
        i, depth, out, q = m.end(), 1, [], None
        while i < len(src) and depth:
            c = src[i]
            if q:
                if c == "\\":
                    out.append(src[i:i + 2]); i += 2; continue
                if c == q:
                    q = None
            elif c in "\"'":
                q = c
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if not depth:
                    break
            out.append(c)
            i += 1
        yield src.count("\n", 0, m.start()) + 1, "".join(out)


def main():
    cap = msg_size()
    bad = n = 0
    lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
    for fn in sorted(os.listdir(ROOT)):
        if not fn.endswith(".c"):
            continue
        src = open(os.path.join(ROOT, fn)).read()
        for line, args in calls(src, "ui_message"):
            for s in lit.findall(args):
                n += 1
                if len(s) > cap:
                    print("%s:%d: %d > %d: \"%s\"" % (fn, line, len(s), cap, s)); bad += 1
        for line, args in calls(src, "ui_say"):
            parts = lit.findall(args)
            if len(parts) >= 2 and args.count(",") == 1 and args.strip().startswith('"'):
                n += 1
                s = parts[0] + parts[1]
                if len(s) > cap:
                    print("%s:%d: %d > %d: \"%s\"" % (fn, line, len(s), cap, s)); bad += 1
            else:
                for s in parts:
                    n += 1
                    if len(s) > cap:
                        print("%s:%d: %d > %d: \"%s\"" % (fn, line, len(s), cap, s)); bad += 1
        for m in re.finditer(r"#define\s+\w*_MSG\s+\"([^\"]*)\"", src):
            n += 1
            if len(m.group(1)) > cap:
                print("%s: %d > %d: \"%s\"" % (fn, len(m.group(1)), cap, m.group(1))); bad += 1
    print("beatit_msg_len: %d messages checked, %d longer than %d characters" % (n, bad, cap))
    return bad


if __name__ == "__main__":
    sys.exit(main())
