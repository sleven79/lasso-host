#!/usr/bin/env python

# This file is used to generate a .h file with the last git commit's revision number.

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
    revnum = rx_non_decimal.sub('.', rev).split('.')[1:4]
    #print(rx_non_decimal.sub('.', rev))
    #print(revnum)
    with open('src/lasso_version.h', 'w') as out:
        out.write(template % {'revision_num': '{}{}{}'.format(*[str.zfill(4) for str in revnum]),
                              'revision_str': rev[1:]})

gitrev()