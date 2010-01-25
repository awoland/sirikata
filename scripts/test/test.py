#!/usr/bin/python
#
# test.py - Individual system test

import subprocess
import time
import re

class Test:
    def __init__(self, _name):
        self.name = _name

    def run(self):
        print "Run not implemented for test " + self.name



class ShellCommandTest(Test):
    # _name: Name of the test
    # _cmd: Command to execute, as list of program and args
    # _warnings: List of keywords to match in stderr and stdout that indicate a warning condition
    # _errors: List of keywords to match in stderr and stdout that indicate an error
    def __init__(self, _name, _cmd, _warnings=['[Ww]arning'], _errors=['[Ee]rror','[Aa]ssert']):
        Test.__init__(self, _name)
        self.cmd = _cmd
        self.warnings = _warnings
        self.errors = _errors

    def run(self):
        sp = subprocess.Popen(self.cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        result = ''
        while( sp.returncode == None ):
            sp.poll()
            (stdoutdata, stderrdata) = sp.communicate()
            if stdoutdata != None:
                result += stdoutdata
            time.sleep(0.1)

        # now check for warnings and errors
        has_errors = self.__check_errors(result)
        has_warnings = self.__check_warnings(result)

        if has_errors:
            print self.name, "Failed"
            return False

        if has_warnings:
            print self.name, "Succeeded with warnings"
        else:
            print self.name, "Succeeded"
        return True

    def __check_warnings(self, output):
        return self.__check_output(output, self.warnings)

    def __check_errors(self, output):
        return self.__check_output(output, self.errors)

    def __check_output(self, output, matchers):
        matched = False
        outputlines = output.splitlines()
        for matcher in matchers:
            for line in outputlines:
                found = re.search(matcher, line)
                if found == None:
                    continue
                print line
                matched = True
        return matched
