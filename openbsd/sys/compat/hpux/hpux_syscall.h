/*
 * System call numbers.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * created from	OpenBSD: syscalls.master,v 1.5 1997/03/26 08:11:09 downsj Exp 
 */

#define	HPUX_SYS_syscall	0
#define	HPUX_SYS_exit	1
#define	HPUX_SYS_fork	2
#define	HPUX_SYS_read	3
#define	HPUX_SYS_write	4
#define	HPUX_SYS_open	5
#define	HPUX_SYS_close	6
#define	HPUX_SYS_wait	7
#define	HPUX_SYS_creat	8
#define	HPUX_SYS_link	9
#define	HPUX_SYS_unlink	10
#define	HPUX_SYS_execv	11
#define	HPUX_SYS_chdir	12
#define	HPUX_SYS_time_6x	13
#define	HPUX_SYS_mknod	14
#define	HPUX_SYS_chmod	15
#define	HPUX_SYS_chown	16
#define	HPUX_SYS_obreak	17
#define	HPUX_SYS_stat_6x	18
#define	HPUX_SYS_lseek	19
#define	HPUX_SYS_getpid	20
#define	HPUX_SYS_setuid	23
#define	HPUX_SYS_getuid	24
#define	HPUX_SYS_stime_6x	25
#define	HPUX_SYS_ptrace	26
#define	HPUX_SYS_alarm_6x	27
#define	HPUX_SYS_fstat_6x	28
#define	HPUX_SYS_pause_6x	29
#define	HPUX_SYS_utime_6x	30
#define	HPUX_SYS_stty_6x	31
#define	HPUX_SYS_gtty_6x	32
#define	HPUX_SYS_access	33
#define	HPUX_SYS_nice_6x	34
#define	HPUX_SYS_ftime_6x	35
#define	HPUX_SYS_sync	36
#define	HPUX_SYS_kill	37
#define	HPUX_SYS_stat	38
#define	HPUX_SYS_setpgrp_6x	39
#define	HPUX_SYS_lstat	40
#define	HPUX_SYS_dup	41
#define	HPUX_SYS_pipe	42
#define	HPUX_SYS_times_6x	43
#define	HPUX_SYS_profil	44
#define	HPUX_SYS_setgid	46
#define	HPUX_SYS_getgid	47
#define	HPUX_SYS_ssig_6x	48
#define	HPUX_SYS_ioctl	54
#define	HPUX_SYS_symlink	56
#define	HPUX_SYS_utssys	57
#define	HPUX_SYS_readlink	58
#define	HPUX_SYS_execve	59
#define	HPUX_SYS_umask	60
#define	HPUX_SYS_chroot	61
#define	HPUX_SYS_fcntl	62
#define	HPUX_SYS_ulimit	63
#define	HPUX_SYS_vfork	66
#define	HPUX_SYS_vread	67
#define	HPUX_SYS_vwrite	68
#define	HPUX_SYS_mmap	71
#define	HPUX_SYS_munmap	73
#define	HPUX_SYS_mprotect	74
#define	HPUX_SYS_getgroups	79
#define	HPUX_SYS_setgroups	80
#define	HPUX_SYS_getpgrp2	81
#define	HPUX_SYS_setpgrp2	82
#define	HPUX_SYS_setitimer	83
#define	HPUX_SYS_wait3	84
#define	HPUX_SYS_getitimer	86
#define	HPUX_SYS_dup2	90
#define	HPUX_SYS_fstat	92
#define	HPUX_SYS_select	93
#define	HPUX_SYS_fsync	95
#define	HPUX_SYS_sigreturn	103
#define	HPUX_SYS_sigvec	108
#define	HPUX_SYS_sigblock	109
#define	HPUX_SYS_sigsetmask	110
#define	HPUX_SYS_sigpause	111
#define	HPUX_SYS_sigstack	112
#define	HPUX_SYS_gettimeofday	116
#define	HPUX_SYS_readv	120
#define	HPUX_SYS_writev	121
#define	HPUX_SYS_settimeofday	122
#define	HPUX_SYS_fchown	123
#define	HPUX_SYS_fchmod	124
#define	HPUX_SYS_setresuid	126
#define	HPUX_SYS_setresgid	127
#define	HPUX_SYS_rename	128
#define	HPUX_SYS_truncate	129
#define	HPUX_SYS_ftruncate	130
#define	HPUX_SYS_sysconf	132
#define	HPUX_SYS_mkdir	136
#define	HPUX_SYS_rmdir	137
#define	HPUX_SYS_getrlimit	144
#define	HPUX_SYS_setrlimit	145
#define	HPUX_SYS_rtprio	152
#define	HPUX_SYS_netioctl	154
#define	HPUX_SYS_lockf	155
#define	HPUX_SYS_semget	156
#define	HPUX_SYS___semctl	157
#define	HPUX_SYS_semop	158
#define	HPUX_SYS_msgget	159
#define	HPUX_SYS_msgctl	160
#define	HPUX_SYS_msgsnd	161
#define	HPUX_SYS_msgrcv	162
#define	HPUX_SYS_shmget	163
#define	HPUX_SYS_shmctl	164
#define	HPUX_SYS_shmat	165
#define	HPUX_SYS_shmdt	166
#define	HPUX_SYS_advise	167
#define	HPUX_SYS_getcontext	174
#define	HPUX_SYS_getaccess	190
#define	HPUX_SYS_waitpid	200
#define	HPUX_SYS_pathconf	225
#define	HPUX_SYS_fpathconf	226
#define	HPUX_SYS_getdirentries	231
#define	HPUX_SYS_getdomainname	232
#define	HPUX_SYS_setdomainname	236
#define	HPUX_SYS_sigaction	239
#define	HPUX_SYS_sigprocmask	240
#define	HPUX_SYS_sigpending	241
#define	HPUX_SYS_sigsuspend	242
#define	HPUX_SYS_getdtablesize	268
#define	HPUX_SYS_fchdir	272
#define	HPUX_SYS_accept	275
#define	HPUX_SYS_bind	276
#define	HPUX_SYS_connect	277
#define	HPUX_SYS_getpeername	278
#define	HPUX_SYS_getsockname	279
#define	HPUX_SYS_getsockopt	280
#define	HPUX_SYS_listen	281
#define	HPUX_SYS_recv	282
#define	HPUX_SYS_recvfrom	283
#define	HPUX_SYS_recvmsg	284
#define	HPUX_SYS_send	285
#define	HPUX_SYS_sendmsg	286
#define	HPUX_SYS_sendto	287
#define	HPUX_SYS_setsockopt2	288
#define	HPUX_SYS_shutdown	289
#define	HPUX_SYS_socket	290
#define	HPUX_SYS_socketpair	291
#define	HPUX_SYS_nsemctl	312
#define	HPUX_SYS_nmsgctl	313
#define	HPUX_SYS_nshmctl	314
#define	HPUX_SYS_MAXSYSCALL	315