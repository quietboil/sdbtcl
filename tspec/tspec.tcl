package provide tspec 1.0

namespace eval tspec {

    array set color {
        reset   ""
        feature ""
        OK      ""
        FAIL    ""
        SKIP    ""
        INFO    ""
    }
    set colorize 0
    set match ""

    proc init { args } {
        variable colorize
        variable color
        variable match

        array set res $args
        array set res {-only {}}

        foreach arg $::argv {
            if { $arg in {-c -color} } {
                set colorize 1
            } elseif { [info exists opt] } {
                set res($opt) $arg
                unset opt
            } elseif { [string index $arg 0] != "-" || [string length $arg] < 2 || ![info exists res($arg)] } {
                return -code error "Unrecogized option $arg"
            } else {
                set opt $arg
            }
        }

        if { $colorize } {
            array set color {
                reset   "\033\[0m"
                feature "\033\[36;4m"
                OK      "\033\[32m"
                FAIL    "\033\[31m"
                SKIP    "\033\[90m"
                INFO    "\033\[30m"
            }
        }

        set match $res(-only)
        unset res(-only)
        return [array get res]
    }

    proc report { msg err {retval {}}} {
        variable colorize
        variable color

        switch $err {
                  1 { set res FAIL }
                  3 { set res SKIP }
            default { set res OK   }
        }
        if { $colorize } {
            puts [format "  %s%s%s" $color($res) $msg $color(reset)]
        } else {
            if { $msg == "" } {
                set msg $res
                set res ""
            }
            puts [format "  %-62s%s" $msg $res]
        }
        if { $err != 0 && [string length $retval] > 0 } {
            puts -nonewline "    "
            if { $::errorCode ne "NONE" } {
                puts -nonewline "$::errorCode: "
            }
            puts [format "%s%s%s" $color(INFO) $retval $color(reset)]
        }
    }

    proc present { feature } {
        variable color
        puts [format %s%s%s $color(feature) $feature $color(reset)]
    }

    proc shouldRun { testDescr } {
        variable match

        return [regexp $match $testDescr]
    }
}

##
# Executes a test
# Usage:
#   describe "what is being tested" {
#        prologue { code to set up the test environment }
#        it "does something" { test case implementation that returns ok or error }
#        it "also does something else" { ... }
#        it "can do other things" { ... }
#        epilogue { code to tear down the test environment }
#   }
#   prologue and epilogue are optional.
##
proc describe { feature test_cases } {
    tspec::present $feature

    set prologue_idx [lsearch $test_cases "prologue"]
    if { $prologue_idx >= 0 } {
        set code [lindex $test_cases $prologue_idx+1]
        switch [catch {apply [list {} $code]} res] {
            1 {
                tspec::report "prologue" 1 $res
                return
            }
            3 {
                tspec::report "" 3
                return
            }
        }
    }

    set epilogue_code {}
    set num_args [llength $test_cases]
    for { set i 0 } { $i < $num_args } { incr i } {
        set opt [lindex $test_cases $i]
        switch -- $opt {
            prologue {
                incr i
            }
            epilogue {
                set epilogue_code [lindex $test_cases [incr i]]
            }
            it {
                set desc [lindex $test_cases [incr i]]
                if {[tspec::shouldRun $desc]} {
                    set code [lindex $test_cases [incr i]]
                    set err [catch {apply [list {} $code]} res]
                    tspec::report $desc $err $res
                } else {
                    tspec::report $desc 3
                }
            }
        }
    }

    if { $epilogue_code ne "" } {
        if { [catch {apply [list {} $epilogue_code]} res] == 1 } {
            tspec::report "epilogue" 1 $res
        }
    }
    return
}

##
# Executes a generic assertion code
#
proc expect { title test_code } {
    set err [catch {uplevel $test_code} res]
    if { $err == 1 } {
        return -code error "expectation '$title' failed - $res"
    } elseif { $res != 1 } {
        return -code error "expectation '$title' is not met"
    }
    return
}

##
# Executes a specific assertion
#
proc assert { title actual op expected } {
    if { [catch {expr "\$actual $op \$expected"} res] } {
        return -code error "assertion '$title' failed - $res"
    } elseif { $res != 1 } {
        return -code error "assertion '$title' ($actual $op $expected) is not true"
    }
    return
}
