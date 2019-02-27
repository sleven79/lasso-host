#!/usr/bin/env python

import os
import subprocess

gitpath = os.getenv('GITPATH', 'C:/Users/s.leven/AppData/Local/GitHubDesktop/app-1.6.2/resources/app/git/cmd')

def get_git_revision_hash():
    return subprocess.check_output([gitpath + '/git', 'rev-parse', 'HEAD'])

def get_git_revision_short_hash():
    return subprocess.check_output([gitpath + '/git', 'rev-parse', '--short', 'HEAD'])

def get_git_describe():
    return subprocess.check_output([gitpath + '/git', 'describe', '--always'])
    
print(gitpath)
print(get_git_revision_hash().strip())
print(get_git_revision_short_hash().strip())
print(get_git_describe().strip())
