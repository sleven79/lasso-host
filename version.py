#!/usr/bin/env python

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
    rx_non_decimal = re.compile(r'[^\d.]+')
    with open('src/lasso_version.h', 'w') as out:
        out.write(template % {'revision_num': rx_non_decimal.sub('', rev),
                              'revision_str': rev[1:]})

gitrev()