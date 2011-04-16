"""
    py-sysload
    ==========

    A simple uptime, cpu, mem, swap getter for Unix-like systems,
    inspired by xfce4-systemload-pligin.

    :copyright: (c) 2011 by Zhihao Yuan.
    :license: 2-clause BSD License.
    :version: 0.1a
"""

from __future__ import with_statement
from ctypes import *
from ctypes.util import find_library
from time import time
from inspect import isclass
from contextlib import contextmanager
from warnings import warn
import os, sys
import re

__all__ = 'uptime cpuload memswap sysctl sysctlbyname libc'.split()

try:
    libc = CDLL(find_library('c'), use_errno=True)
except TypeError:
    libc = CDLL(find_library('c'))
    libc.__errno_location.restype = POINTER(c_int)
    # linux2 only
    def get_errno():
        return libc.__errno_location().contents.value

if sys.platform.startswith('freebsd'):
    libkvm = CDLL('libkvm.so')
    libkvm.kvm_open.restype = c_void_p

    @contextmanager
    def kvm_open():
        kd = cast(libkvm.kvm_open('/dev/null', '/dev/null', 
                '/dev/null', os.O_RDONLY, 'kvm_open'), c_void_p)
        yield kd
        libkvm.kvm_close(kd)

    class kvm_swap(Structure):
        _fields_ = [("devname", c_char * 32),
                    ("used", c_int),
                    ("total", c_int),
                    ("flags", c_int)]

CTL_KERN      = 1
CTL_VM        = 2
# FIXME may not be portable
KERN_BOOTTIME = 21

class timeval(Structure):
    _fields_ = [("sec", c_int64),
                ("usec", c_long)]

def uptime():
    try:
        with open('/proc/uptime') as f:
            return int(float(f.read().split()[0]))
    except IOError:
        return int(time()) - sysctl((CTL_KERN, KERN_BOOTTIME), timeval).sec

def cpuload():
    try:
        with open('/proc/stat') as f:
            l = map(int, f.readline().split()[1:])
            l.extend([0] * (8-len(l)))
        total = sum(l)
        used = total - (sum(l[3:5]))
    except IOError:
        l = sysctlbyname('kern.cp_time', c_long * 5)
        total = sum(l)
        used = total - l[4]
    return used, total

def memswap():
    mtotal, mused, stotal, sused = [0] * 4
    try:
        with open('/proc/meminfo') as f:
            vlen = 0
            for l in f.readlines():
                m = re.match(r'(\w+):\s*(\d+)', l)
                if not m: continue
                k, v = m.groups()
                if k == 'MemTotal':
                    mtotal = int(v)
                    mused += mtotal
                    vlen += 1
                elif k in ('MemFree', 'Buffers', 'Cached'):
                    mused -= int(v)
                    vlen += 1
                elif k == 'SwapTotal':
                    stotal = int(v)
                    sused += stotal
                    vlen += 1
                elif k == 'SwapFree':
                    sused -= int(v)
                    vlen += 1
                else: pass
                if vlen > 5: break
        return mused, mtotal, sused, stotal
    except IOError:
        psize = libc.getpagesize()
        def CONVERT(v): return v * psize / 1024
        # freebsd only
        if sys.platform.startswith('freebsd'):
            mtotal = CONVERT(
                    sysctlbyname('vm.stats.vm.v_page_count', c_int))
            mused = mtotal - CONVERT(
                    sysctlbyname('vm.stats.vm.v_free_count', c_int) +
                    sysctlbyname('vm.stats.vm.v_inactive_count', c_int))
            with kvm_open() as kd:
                if kd.value == None:
                    warn('kvm_open() failed')
                else:
                    swap = (kvm_swap * 1)()
                    if libkvm.kvm_getswapinfo(kd, swap, 1, 0) == 0:
                        stotal = CONVERT(swap[0].total)
                        sused = CONVERT(swap[0].used)
        return mused, mtotal, sused, stotal

def sysctl(mib_t, c_type=None):
    mib = (c_int * len(mib_t))()
    for i, v in enumerate(mib_t):
        mib[i] = c_int(v)
    if c_type == None:
        sz = c_size_t(0)
        libc.sysctl(mib, len(mib), None, byref(sz), None, 0)
        buf = create_string_buffer(sz.value)
    else:
        buf = c_type(0)
        sz = c_size_t(sizeof(buf))
    st = libc.sysctl(mib, len(mib), byref(buf), byref(sz), None, 0)
    if st != 0:
        raise LibError('sysctl() returned with error %d' % st)
    try:
        return buf.value
    except AttributeError:
        return buf

def sysctlbyname(name, c_type=None):
    if c_type == None:
        sz = c_size_t(0)
        libc.sysctlbyname(name, None, byref(sz), None, 0)
        buf = create_string_buffer(sz.value)
    else:
        buf = c_type(0)
        sz = c_size_t(sizeof(buf))
    st = libc.sysctlbyname(name, byref(buf), byref(sz), None, 0)
    if st != 0:
        raise LibError('sysctlbyname() returned with error %d' % st)
    try:
        return buf.value
    except AttributeError:
        return buf

class LibError(OSError):
    def __init__(self, *args):
        OSError.__init__(self, get_errno(), *args)
