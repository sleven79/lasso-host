#!/usr/bin/env python

# This file is used to generate the lasso_version.h file with the last git commit's revision number.
# The file is produced based on template lasso_version.in.h.

# Valid Git revision strings (git describe) are: v<major>.<minor>.<patch>[-alpha | -beta | -rc.x]-<# commits>-<g"rev hash">
# where:
# 1) v, <major>, . , <minor>, -alpha, -beta, -rc.x are attributes assigned by the user for a specific tag
# 2) <# commits> is the number of additional commits applied to a branch after the last tag
# 3) <g"rev-hash"> is the revision hash from "git rev-parse --short HEAD"
#
# From semver.org (Semantic Versioning)
# <major> version change when you make incompatible APi changes
# <minor> version change when you add functionality in a backwards-compatible manner, and
# <patch> version change when you make backwards-compatible bug fixes
#
# Examples:
# v0.1.0
# v0.1.0-alpha
# v0.1.0-alpha-14-g2698as
# v1.0.0-rc.2

import os
import subprocess
import re

gitpath = os.getenv('GITPATH', 'C:/Users/s.leven/AppData/Local/GitHubDesktop/app-1.6.2/resources/app/git/cmd')

def get_git_revision_hash():
    return subprocess.check_output([gitpath + '/git', 'rev-parse', 'HEAD'])

def get_git_revision_short_hash():
    return subprocess.check_output([gitpath + '/git', 'rev-parse', '--short', 'HEAD'])

def get_git_describe():
    val = subprocess.check_output([gitpath + '/git', 'describe', '--tags', '--always'])
    #print(val)
    return val.decode()

def gitrev():
    """Generate the header file that contains the git revision."""
    f = open('src/lasso_version.h.in')
    template = f.read()
    rev = get_git_describe()
    rx_non_decimal = re.compile(r'[\D.]+')
    # extract 3 first numbers from rev (major, minor, patch)
    # 1) replace all non-digit chars and chains of chars by a single '.'
    # 2) split string along '.'
    # 3) extract elements 1-4 (element 0 is empty string)
    revnum = rx_non_decimal.sub('.', rev)
    revnum = revnum.split('.')
    if len(revnum) == 6:
        revnum = revnum[1:5]    # if <# commits> tag available
    else:
        revnum = revnum[1:4] + ['0']
    #print(rx_non_decimal.sub('.', rev))
    #print(revnum)
    with open('src/lasso_version.h', 'w') as out:
        out.write(template % {'revision_num': '0x{:02X}{:02X}{:02X}{:02X}'.format(*[int(r) for r in revnum]),
                              'revision_str': rev[1:]})

gitrev()