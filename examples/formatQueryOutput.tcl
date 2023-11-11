# @param columnInfo - output of the db columns with a list of lists of column meta data
# @return format string to format a fetched row
# @note this procedure outputs (to stdout) the result table header
proc formatQueryOutput { columnsInfo } {
    foreach colInfo $columnsInfo {
        array set col $colInfo
        set colLength [expr max($col(length),[string length $col(label)])]
        puts -nonewline [format "| %-${colLength}s " $col(label)]
        append underline "|-" [string repeat "-" $colLength] "-"
        append rowFormat "| "
        if { $col(type) eq "FIXED" } {
            append rowFormat %$colLength.$col(scale)f
        } else {
            append rowFormat %-${colLength}s
        }
        append rowFormat " "
    }
    puts "|"
    append rowFormat "|"
    append underline "|"
    puts $underline
    return $rowFormat
}