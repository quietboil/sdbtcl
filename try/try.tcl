package provide try 1.0

##
# try
#   Executes a block of code that might return an error.
#   Always executes finalization code irrespective of the error.
#
# Synopsis:
#   try { code } finally { code }
#
# Example:
#   set fileHandle [open $fileName r]
#   try {
#       set text [read $fileHandle]
#   } finally {
#       close $fileHandle
#   }
##
proc try { tryCode keywordFinally finalizationCode } {
    if { $keywordFinally ne "finally" } {
        return -code error "invalid try-finally syntax"
    }
    set err [catch {uplevel $tryCode} result opts]
    uplevel $finalizationCode
    if { $err } {
        dict incr opts -level
    }
    return {*}$opts $result
}
