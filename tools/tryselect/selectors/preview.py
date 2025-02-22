# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""This script is intended to be called through fzf as a preview formatter."""

from __future__ import absolute_import, print_function

import argparse
import os
import sys

here = os.path.abspath(os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(here), 'util'))
from estimates import duration_summary, task_duration_data


def process_args():
    """Process preview arguments."""
    argparser = argparse.ArgumentParser()
    argparser.add_argument('-s', '--show-estimates', action="store_true")
    argparser.add_argument('-g', '--graph-cache', type=str, default=None)
    argparser.add_argument('-c', '--cache_dir', type=str, default=None)
    argparser.add_argument('tasklist', type=str)
    return argparser.parse_args()


def plain_display(tasklist):
    """Original preview window display."""
    print("\n".join(sorted(s.strip("'") for s in tasklist.split())))


def duration_display(graph_cache_file, tasklist, cache_dir):
    """Preview window display with task durations + metadata."""
    tasklist = [t.strip("'") for t in tasklist.split()]

    durations = duration_summary(graph_cache_file, tasklist, cache_dir)
    output = ""
    max_columns = int(os.environ['FZF_PREVIEW_COLUMNS'])

    output += "\nSelected tasks take {}\n".format(durations["selected_duration"])
    output += "+{} dependencies, total {}\n".format(
        durations["dependency_count"],
        durations["selected_duration"] + durations["dependency_duration"])

    if durations.get("quantile"):
        output += "This is in the top {}% of requests\n".format(durations["quantile"])

    output += "Estimated finish in {} at {}".format(
        durations["wall_duration_seconds"],
        durations["eta_datetime"].strftime("%H:%M"))

    duration_width = 5  # show five numbers at most.
    task_durations = task_duration_data(cache_dir)
    output += "{:>{width}}\n".format("Duration", width=max_columns)
    for task in tasklist:
        duration = int(task_durations.get(task, 0.0))
        output += "{:{align}{width}} {:{nalign}{nwidth}}s\n".format(
            task,
            duration,
            align='<',
            width=max_columns-(duration_width+2),  # 2: space and 's'
            nalign='>',
            nwidth=duration_width,
        )

    print(output)


if __name__ == "__main__":
    args = process_args()
    if args.show_estimates and os.path.isdir(args.cache_dir):
        duration_display(args.graph_cache, args.tasklist, args.cache_dir)
    else:
        plain_display(args.tasklist)
