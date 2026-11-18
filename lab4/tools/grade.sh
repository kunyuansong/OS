#!/bin/sh

verbose=false
if [ "x$1" = "x-v" ]; then
    verbose=true
    out=/dev/stdout
    err=/dev/stderr
else
    out=/dev/null
    err=/dev/null
fi

## make & makeopts
if gmake --version > /dev/null 2>&1; then
    make=gmake;
else
    make=make;
fi

makeopts="--quiet --no-print-directory -j"

make_print() {
    echo `$make $makeopts print-$1`
}

echo "Building grading kernel..."
if ! $make $makeopts > .grade-build.log 2>&1; then
    echo "Build failed (.grade-build.log):"
    cat .grade-build.log
    exit 1
fi
if $verbose; then
    cat .grade-build.log
fi

## command tools
awk='awk'
bc='bc'
date='date'
grep='grep'
rm='rm -f'
sed='sed'

## symbol table
sym_table='obj/kernel.sym'

## qemu & qemuopts
qemu="$(make_print qemu)"

qemu_out="$(make_print GRADE_QEMU_OUT)"
qemu_err="${qemu_out}.err"

## default variables
default_timeout=30
default_pts=5

pts=5
part=0
part_pos=0
total=0
total_pos=0

## default functions
update_score() {
    total=`expr $total + $part`
    total_pos=`expr $total_pos + $part_pos`
    part=0
    part_pos=0
}

get_time() {
    echo `$date +%s.%N 2> /dev/null`
}

show_part() {
    echo "Part $1 Score: $part/$part_pos"
    echo
    update_score
}

show_final() {
    update_score
    echo "Total Score: $total/$total_pos"
    if [ $total -lt $total_pos ]; then
        exit 1
    fi
}

show_time() {
    t1=$(get_time)
    time=`echo "scale=1; ($t1-$t0)/1" | $sed 's/.N/.0/g' | $bc 2> /dev/null`
    echo "(${time}s)"
}

show_build_tag() {
    echo "$1:" | $awk '{printf "%-24s ", $0}'
}

show_check_tag() {
    echo "$1:" | $awk '{printf "  -%-40s  ", $0}'
}

show_msg() {
    echo $1
    shift
    if [ $# -gt 0 ]; then
        echo -e "$@" | awk '{printf "   %s\n", $0}'
        echo
    fi
}

pass() {
    show_msg OK "$@"
    part=`expr $part + $pts`
    part_pos=`expr $part_pos + $pts`
}

fail() {
    show_msg WRONG "$@"
    part_pos=`expr $part_pos + $pts`
}

stop_qemu() {
    if [ -n "$pid" ]; then
        kill "$pid" > /dev/null 2>&1
        wait "$pid" > /dev/null 2>&1
        pid=
    fi
}

run_qemu() {
    # Start immediately. Grading needs serial output, not a GDB connection.
    # The kernel monitor is reached after do_exit or an assertion failure;
    # the unchanged checks below decide whether the tests actually passed.
    if [ -z "$timeout" ] || [ $timeout -le 0 ]; then
        timeout=$default_timeout;
    fi

    t0=$(get_time)
    : > "$qemu_out"
    : > "$qemu_err"
    pid=
    trap 'stop_qemu' 0
    trap 'stop_qemu; exit 130' INT
    trap 'stop_qemu; exit 143' TERM
    (
        exec $qemu -nographic $qemuopts -serial "file:$qemu_out" -monitor null -no-reboot
    ) > "$out" 2> "$qemu_err" &
    pid=$!

    elapsed=0
    while kill -0 "$pid" > /dev/null 2>&1; do
        if grep -Fq 'Welcome to the kernel debug monitor!!' "$qemu_out"; then
            break
        fi
        if [ "$elapsed" -ge "$timeout" ]; then
            echo "QEMU timed out after ${timeout}s; inspect $qemu_out"
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    stop_qemu
    if [ -s "$qemu_err" ]; then
        echo "QEMU diagnostics ($qemu_err):"
        cat "$qemu_err"
    fi
}

build_run() {
    # usage: build_run <tag> <args>
    show_build_tag "$1"
    shift

    if $verbose; then
        echo "$make $@ ..."
    fi
    $make $makeopts $@ 'DEFS+=-DDEBUG_GRADE' > $out 2> $err

    if [ $? -ne 0 ]; then
        echo $make $@ failed
        exit 1
    fi

    # now run qemu and save the output
    run_qemu

    show_time

    cp $qemu_out .`echo $tag | tr '[:upper:]' '[:lower:]' | sed 's/ /_/g'`.log
}

check_result() {
    # usage: check_result <tag> <check> <check args...>
    show_check_tag "$1"
    shift

    if [ ! -s "$qemu_out" ]; then
        fail "No serial output in $qemu_out; inspect $qemu_err"
    else
        check=$1
        shift
        $check "$@"
    fi
}

check_regexps() {
    okay=yes
    not=0
    reg=0
    error=
    for i do
        if [ "x$i" = "x!" ]; then
            not=1
        elif [ "x$i" = "x-" ]; then
            reg=1
        else
            if [ $reg -ne 0 ]; then
                $grep '-E' "^$i\$" $qemu_out > /dev/null
            else
                $grep '-F' "$i" $qemu_out > /dev/null
            fi
            found=$(($? == 0))
            if [ $found -eq $not ]; then
                if [ $found -eq 0 ]; then
                    msg="!! error: missing '$i'"
                else
                    msg="!! error: got unexpected line '$i'"
                fi
                okay=no
                if [ -z "$error" ]; then
                    error="$msg"
                else
                    error="$error\n$msg"
                fi
            fi
            not=0
            reg=0
        fi
    done
    if [ "$okay" = "yes" ]; then
        pass
    else
        fail "$error"
        if $verbose; then
            exit 1
        fi
    fi
}

run_test() {
    # usage: run_test [-tag <tag>] [-Ddef...] [-check <check>] checkargs ...
    tag=
    check=check_regexps
    while true; do
        select=
        case $1 in
            -tag)
                select=`expr substr $1 2 ${#1}`
                eval $select='$2'
                ;;
        esac
        if [ -z "$select" ]; then
            break
        fi
        shift
        shift
    done
    defs=
    while expr "x$1" : "x-D.*" > /dev/null; do
        defs="DEFS+='$1' $defs"
        shift
    done
    if [ "x$1" = "x-check" ]; then
        check=$2
        shift
        shift
    fi

    $make $makeopts touch > /dev/null 2>&1
    build_run "$tag" "$defs"

    check_result 'check result' "$check" "$@"
}

quick_run() {
    # usage: quick_run <tag> [-Ddef...]
    tag="$1"
    shift
    defs=
    while expr "x$1" : "x-D.*" > /dev/null; do
        defs="DEFS+='$1' $defs"
        shift
    done

    $make $makeopts touch > /dev/null 2>&1
    build_run "$tag" "$defs"
}

quick_check() {
    # usage: quick_check <tag> checkargs ...
    tag="$1"
    shift
    check_result "$tag" check_regexps "$@"
}

## kernel image
osimg=$(make_print ucoreimg)

## swap image
swapimg=$(make_print swapimg)

## set default qemu-options
qemuopts="-machine virt -nographic -bios default -kernel bin/ucore.img"

run_qemu

## check now!!

# quick_run 'Check VMM'

pts=10
quick_check 'check PMM'                                     \
    '[Test PMM] PASS'

pts=10
quick_check 'check Process Create'                          \
    '[Test Process Create] PASS'

pts=10
quick_check 'check Schedule'                                \
    '[Test Schedule] PASS'                                   \
    'this initproc, pid = 1, name = "init"'                 \
    'To U: "Hello world!!".'                                \
    'To U: "en.., Bye, Bye. :)"'

## print final-score
show_final
