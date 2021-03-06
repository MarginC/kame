/*
 * Copyright (c) 1997 John S. Dyson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. John S. Dyson's name may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * DISCLAIMER:  This code isn't warranted to do anything useful.  Anything
 * bad that happens because of using this software isn't the responsibility
 * of the author.  This software is distributed AS-IS.
 *
 * $FreeBSD: src/sys/sys/aio.h,v 1.13 2000/01/14 02:53:28 jasone Exp $
 */

#ifndef _SYS_AIO_H_
#define	_SYS_AIO_H_

#include <sys/time.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socketvar.h>

/*
 * Returned by aio_cancel:
 *  (Note that FreeBSD's aio is not cancellable -- yet.)
 */
#define	AIO_CANCELED		0x1
#define	AIO_NOTCANCELED		0x2
#define	AIO_ALLDONE		0x3

/*
 * LIO opcodes
 */
#define	LIO_NOP			0x0
#define LIO_WRITE		0x1
#define	LIO_READ		0x2

/*
 * LIO modes
 */
#define	LIO_NOWAIT		0x0
#define	LIO_WAIT		0x1

/*
 * Maximum number of allowed LIO operations
 */
#define	AIO_LISTIO_MAX		16

/*
 * Private mode bit for aio.
 * (This bit is set by the library routine
 *  to allow the kernel to support sync
 *  or async operations in the future.)
 */
#define AIO_PMODE_SYNC		0x1
#define AIO_PMODE_DONE		0x2
#define AIO_PMODE_SUSPEND	0x4
#define AIO_PMODE_ACTIVE	0x2357c0de

/*
 * Private members for aiocb -- don't access
 * directly.
 */
struct __aiocb_private {
	int	status;
	int	error;
	int	privatemodes;
	int	active;
	int	tid;
	int	threadinfo;
	void	*userinfo;
	void	*kernelinfo;
};

/*
 * I/O control block
 */
typedef struct aiocb {
	int	aio_fildes;		/* File descriptor */
	off_t	aio_offset;		/* File offset for I/O */
	volatile void *aio_buf;         /* I/O buffer in process space */
	size_t	aio_nbytes;		/* Number of bytes for I/O */
	struct	sigevent aio_sigevent;	/* Signal to deliver */
	int	aio_lio_opcode;		/* LIO opcode */
	int	aio_reqprio;		/* Request priority -- ignored */
	struct	__aiocb_private	_aiocb_private;
} aiocb_t;

#ifndef _KERNEL

__BEGIN_DECLS
/*
 * Asynchronously read from a file
 */
int	aio_read(struct aiocb *);

/*
 * Asynchronously write to file
 */
int	aio_write(struct aiocb *);

/*
 * List I/O Asynchronously/synchronously read/write to/from file
 *	"lio_mode" specifies whether or not the I/O is synchronous.
 *	"acb_list" is an array of "nacb_listent" I/O control blocks.
 *	when all I/Os are complete, the optional signal "sig" is sent.
 */
int	lio_listio(int, struct aiocb * const [], int, struct sigevent *);

/*
 * Get completion status
 *	returns EINPROGRESS until I/O is complete.
 *	this routine does not block.
 */
int	aio_error(const struct aiocb *);

/*
 * Finish up I/O, releasing I/O resources and returns the value
 *	that would have been associated with a synchronous I/O request.
 *	This routine must be called once and only once for each
 *	I/O control block who has had I/O associated with it.
 */
ssize_t	aio_return(struct aiocb *);

/*
 * Cancel I/O -- implemented only to return AIO_NOTCANCELLED or
 *	AIO_ALLDONE.  No cancellation operation will occur.
 */
int	aio_cancel(int, struct aiocb *);

/*
 * Suspend until all specified I/O or timeout is complete.
 */
int	aio_suspend(const struct aiocb * const[], int, const struct timespec *);

/*
 * Retrieve the status of the specified I/O request.
 */
int	aio_error(const struct aiocb *);

int	aio_waitcomplete(struct aiocb **, struct timespec *);

__END_DECLS

#else
/*
 * Job queue item
 */

#define AIOCBLIST_CANCELLED     0x1
#define AIOCBLIST_RUNDOWN       0x4
#define AIOCBLIST_ASYNCFREE     0x8
#define AIOCBLIST_DONE          0x10

struct aiocblist {
        TAILQ_ENTRY	(aiocblist) list;	/* List of jobs */
        TAILQ_ENTRY	(aiocblist) plist;	/* List of jobs for proc */
        int	jobflags;
        int	jobstate;
        int	inputcharge, outputcharge;
        struct	buf *bp;		/* Buffer pointer */
        struct	proc *userproc;		/* User process */
        struct	file *fd_file;		/* Pointer to file structure */ 
	struct	aioproclist *jobaioproc;/* AIO process descriptor */
        struct	aio_liojob *lio;	/* Optional lio job */
        struct	aiocb *uuaiocb;		/* Pointer in userspace of aiocb */
        struct	aiocb uaiocb;		/* Kernel I/O control block */
};

void	aio_proc_rundown(struct proc *p);

void	aio_swake(struct socket *, struct sockbuf *);

#endif

#endif
