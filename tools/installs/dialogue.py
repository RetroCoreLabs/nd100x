"""Console dialogues as state machines: the way every install script answers ND's
installers (tools/installs/README.md, "Dialogue tables").

A dialogue is a list of states. Each state waits for a SET of valid responses
(When). Each When has its own reply and its own jump (goto) to the next state; a
state may jump to itself (a "Press CR" that comes several times). The rules:

- ALL output after the previous match is searched, with CR left out and $ meaning
  the end of any line, not only the last line on the screen. A question followed by
  a server's console report ("No domino controllers found in system") is still
  found.
- A decision is taken only once the output has been quiet for `quiet` seconds, so a
  question is complete before it is answered. Patterns therefore match a prompt by its
  text and do not need it to end its line: a console report cut by the installer's
  output goes on right after the question, on the same line (install_common.py has
  the screen where that was seen).
- Of the current state's Whens, the one matching EARLIEST in the text wins; on a tie
  the one listed first (the same rule as pexpect).
- A line matching `fail` stops the run at once, before anything else is typed.
- A question that belongs to ANOTHER state (a When with a reply that the current
  state does not have), coming before anything the current state waits for, stops
  the run as "out of order"; it is never answered. Text after the current state's
  match is kept for the next state.
- Everything else ("Wait....", MESSAGE boxes, console reports) is ignored for
  decisions and kept in the transcript.
- Three timeouts, each naming the state and what it waited for: per state (State
  timeout, default 5 minutes), silence (no output at all: a hung machine, default
  10 minutes), and the whole run (default 2 hours).

The states and their Whens are taken from real transcripts of the installer, never
guessed (2026-11-04: a flat "see X, answer Y" list matched against the last line of
the screen sat 10-15 minutes at a question that a console report had pushed up).

The machine only needs poll(wait) -> str, sendline(text) and note(text): an
nd100x_expect.Nd100x, or a stand-in in the unit tests (tests/test_install_dialogue.py).
"""

import re
import time

FLAGS = re.S | re.I | re.M


class DialogueError(Exception):
    """The dialogue stopped. The message names the state and ends with the screen."""


class DialogueFailed(DialogueError):
    """A line matching the fail pattern came."""


class OutOfOrder(DialogueError):
    """A question from another state came; it was not answered."""


class DialogueTimeout(DialogueError):
    """A state, the silence or the whole run took too long."""


class When:
    """One valid response in a state.

    pattern  regex searched in all output since the previous answer (re.S, re.I, re.M;
             CR is left out, so $ is the end of any line)
    reply    the text to type (Return is added); a function (vm, match) that does what
             is needed (eject a floppy) and returns the text; or None to type nothing
             (a prompt that only moves the dialogue on, such as the final @)
    goto     name of the next state (its own name for a question that repeats), or a
             function (match) returning that name; not needed with done=True
    done     True when the dialogue ends here
    """

    def __init__(self, pattern, reply=None, goto=None, done=False):
        if goto is None and not done:
            raise ValueError("When(%r) needs goto= or done=True" % pattern)
        self.pattern = pattern
        self.rx = re.compile(pattern, FLAGS)
        self.reply = reply
        self.goto = goto
        self.done = done

    def is_question(self):
        """True when something is typed for it (only those count as "out of order")."""
        return self.reply is not None


class State:
    """A named state, its valid responses, and how long it may take (seconds)."""

    def __init__(self, name, whens, timeout=300):
        if not whens:
            raise ValueError("state %s has no When" % name)
        self.name = name
        self.whens = list(whens)
        self.timeout = timeout

    def waits_for(self):
        return " | ".join(w.pattern for w in self.whens)


def check_table(states):
    """The states by name. Refuses a repeated name and a goto to a state not in the
    table, so a typing mistake fails when the script starts, not an hour into a run."""
    table = {}
    for s in states:
        if s.name in table:
            raise ValueError("state %s is in the table twice" % s.name)
        table[s.name] = s
    for s in states:
        for w in s.whens:
            if isinstance(w.goto, str) and w.goto not in table:
                raise ValueError("state %s: goto %s, which is not a state" % (s.name, w.goto))
    return table


def _foreign_questions(table):
    """For each state: the questions of the other states that it does not have itself."""
    own = {name: {w.pattern for w in s.whens} for name, s in table.items()}
    foreign = {}
    for name in table:
        found = {}
        for other, s in table.items():
            if other == name:
                continue
            for w in s.whens:
                if w.is_question() and w.pattern not in own[name] and w.pattern not in found:
                    found[w.pattern] = (other, w)
        foreign[name] = list(found.values())
    return foreign


def _earliest(whens, text):
    """(when, match) for the When matching earliest in `text`; on a tie the first listed."""
    best = None
    for w in whens:
        m = w.rx.search(text)
        if m and (best is None or m.start() < best[1].start()):
            best = (w, m)
    return best


def _tail(text, n=800):
    return text[-n:] if text else "(nothing)"


def run_dialogue(vm, states, fail=None, start=None, silence=600, total=7200, quiet=1.0,
                 clock=time.monotonic):
    """Run the dialogue `states` (a list of State) from `start` (default: the first)
    until a When with done=True. `fail`: regex for lines that mean the run failed.
    Returns all output seen (CR left out). Raises DialogueFailed, OutOfOrder or
    DialogueTimeout; each message names the state and shows the screen."""
    table = check_table(states)
    foreign = _foreign_questions(table)
    fail_rx = re.compile(fail, re.I | re.M) if fail else None
    state = table[start or states[0].name]
    began = entered = last_output = clock()
    seen = ""           # output since the previous answer, CR left out
    looked_at = 0       # len(seen) when it was last searched for an answer
    everything = []
    while True:
        out = vm.poll(0.5)
        now = clock()
        if out:
            out = out.replace("\r", "")
            seen += out
            everything.append(out)
            last_output = now
            if fail_rx:
                bad = [line.strip() for line in seen.splitlines() if fail_rx.search(line)]
                if bad:
                    vm.note("[%s] failed: %s" % (state.name, " / ".join(bad)))
                    raise DialogueFailed("state %s: the installer reported a failure:\n  %s"
                                         "\nthe screen ends:\n%s"
                                         % (state.name, "\n  ".join(bad), _tail(seen)))
        if len(seen) != looked_at and now - last_output >= quiet:
            looked_at = len(seen)
            hit = _earliest(state.whens, seen)
            # Only text BEFORE this state's own match can be out of order; what comes
            # after it is kept for the next state.
            for other, w in foreign[state.name]:
                m = w.rx.search(seen)
                if m and (hit is None or m.start() < hit[1].start()):
                    vm.note("[%s] out of order: %r belongs to state %s"
                            % (state.name, m.group(0)[-70:], other))
                    raise OutOfOrder("state %s waits for %s, but %r (a question of state %s) "
                                     "came; not answered.\nthe screen ends:\n%s"
                                     % (state.name, state.waits_for(), m.group(0)[-70:],
                                        other, _tail(seen)))
            if hit:
                w, m = hit
                reply = w.reply(vm, m) if callable(w.reply) else w.reply
                nxt = None if w.done else (w.goto(m) if callable(w.goto) else w.goto)
                if nxt is not None and nxt not in table:
                    raise ValueError("state %s: goto %s, which is not a state" % (state.name, nxt))
                what = ("answer %r to" % reply) if reply is not None else "saw"
                vm.note("[%s] %s %r -> %s" % (state.name, what, m.group(0)[-70:],
                                               "done" if w.done else nxt))
                if reply is not None:
                    vm.sendline(reply)
                if w.done:
                    return "".join(everything)
                # The text after the match stays for the next state (as in Expect):
                # after a When that types nothing, the next prompt may already be there.
                state = table[nxt]
                entered = now
                seen, looked_at = seen[m.end():], -1
                continue
        if now - last_output > silence:
            raise DialogueTimeout("state %s: no output at all for %d s (the machine looks hung); "
                                  "waiting for %s\nthe screen ends:\n%s"
                                  % (state.name, silence, state.waits_for(),
                                     _tail("".join(everything))))
        if now - entered > state.timeout:
            raise DialogueTimeout("state %s: none of its answers came within %d s; waiting for %s"
                                  "\nthe screen ends:\n%s"
                                  % (state.name, state.timeout, state.waits_for(),
                                     _tail(seen or "".join(everything))))
        if now - began > total:
            raise DialogueTimeout("the whole dialogue took more than %d s; in state %s, waiting "
                                  "for %s\nthe screen ends:\n%s"
                                  % (total, state.name, state.waits_for(),
                                     _tail("".join(everything))))
