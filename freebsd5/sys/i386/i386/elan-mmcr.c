/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/i386/elan-mmcr.c,v 1.6 2002/09/17 11:47:38 phk Exp $
 *
 * The AMD Elan sc520 is a system-on-chip gadget which is used in embedded
 * kind of things, see www.soekris.com for instance, and it has a few quirks
 * we need to deal with.
 * Unfortunately we cannot identify the gadget by CPUID output because it
 * depends on strapping options and only the stepping field may be useful
 * and those are undocumented from AMDs side.
 *
 * So instead we recognize the on-chip host-PCI bridge and call back from
 * sys/i386/pci/pci_bus.c to here if we find it.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/timetc.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>

#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>

uint16_t *elan_mmcr;

/* Relating to the /dev/soekris-errled */
static struct mtx errled_mtx;
static char *errled;
static struct callout_handle errled_h = CALLOUT_HANDLE_INITIALIZER(&errled_h);
static void timeout_errled(void *);

static unsigned
elan_get_timecount(struct timecounter *tc)
{
	return (elan_mmcr[0xc84 / 2]);
}

static struct timecounter elan_timecounter = {
	elan_get_timecount,
	0,
	0xffff,
	33333333 / 4,
	"ELAN"
};

void
init_AMD_Elan_sc520(void)
{
	u_int new;
	int i;

	if (bootverbose)
		printf("Doing h0h0magic for AMD Elan sc520\n");
	elan_mmcr = pmap_mapdev(0xfffef000, 0x1000);

	/*-
	 * The i8254 is driven with a nonstandard frequency which is
	 * derived thusly:
	 *   f = 32768 * 45 * 25 / 31 = 1189161.29...
	 * We use the sysctl to get the timecounter etc into whack.
	 */
	
	new = 1189161;
	i = kernel_sysctlbyname(&thread0, "machdep.i8254_freq", 
	    NULL, 0, 
	    &new, sizeof new, 
	    NULL);
	if (bootverbose)
		printf("sysctl machdep.i8254_freq=%d returns %d\n", new, i);

	/* Start GP timer #2 and use it as timecounter, hz permitting */
	elan_mmcr[0xc82 / 2] = 0xc001;
	tc_init(&elan_timecounter);
}


/*
 * Device driver initialization stuff
 */

static d_write_t elan_write;
static d_ioctl_t elan_ioctl;
static d_mmap_t elan_mmap;

#define ELAN_MMCR	0
#define ELAN_ERRLED	1

#define CDEV_MAJOR 100			/* Share with xrpu */
static struct cdevsw elan_cdevsw = {
	/* open */	nullopen,
	/* close */	nullclose,
	/* read */	noread,
	/* write */	elan_write,
	/* ioctl */	elan_ioctl,
	/* poll */	nopoll,
	/* mmap */	elan_mmap,
	/* strategy */	nostrategy,
	/* name */	"elan",
	/* maj */	CDEV_MAJOR,
	/* dump */	nodump,
	/* psize */	nopsize,
	/* flags */	0,
};

static void
elan_drvinit(void)
{

	if (elan_mmcr == NULL)
		return;
	printf("Elan-mmcr driver: MMCR at %p\n", elan_mmcr);
	make_dev(&elan_cdevsw, ELAN_MMCR,
	    UID_ROOT, GID_WHEEL, 0600, "elan-mmcr");
	make_dev(&elan_cdevsw, ELAN_ERRLED,
	    UID_ROOT, GID_WHEEL, 0600, "soekris-errled");
	mtx_init(&errled_mtx, "Elan-errled", MTX_DEF, 0);
	return;
}

SYSINIT(elan, SI_SUB_PSEUDO, SI_ORDER_MIDDLE+CDEV_MAJOR,elan_drvinit,NULL);

#define LED_ON()	do {elan_mmcr[0xc34 / 2] = 0x200;} while(0)
#define LED_OFF()	do {elan_mmcr[0xc38 / 2] = 0x200;} while(0)

static void
timeout_errled(void *p)
{
	static enum {NOTHING, FLASH, DIGIT} mode;
	static int count, cnt2, state;

	mtx_lock(&errled_mtx);
	if (p != NULL) {
		mode = NOTHING;
		/* Our instructions changed */
		if (*errled == '1') {			/* Turn LED on */
			LED_ON();
		} else if (*errled == '0') {		/* Turn LED off */
			LED_OFF();
		} else if (*errled == 'f') {		/* Flash */
			mode = FLASH;
			cnt2 = 10;
			if (errled[1] >= '1' && errled[1] <= '9')		
				cnt2 = errled[1] - '0';
			cnt2 = hz / cnt2;
			LED_ON();
			errled_h = timeout(timeout_errled, NULL, cnt2);
		} else if (*errled == 'd') {		/* Digit */
			mode = DIGIT;
			count = 0;
			cnt2 = 0;
			state = 0;
			LED_OFF();
			errled_h = timeout(timeout_errled, NULL, hz/10);
		}
	} else if (mode == FLASH) {
		if (count) 
			LED_ON();
		else
			LED_OFF();
		count = !count;
		errled_h = timeout(timeout_errled, NULL, cnt2);
	} else if (mode == DIGIT) {
		if (cnt2 > 0) {
			if (state) {
				LED_OFF();
				state = 0;
				cnt2--;
			} else {
				LED_ON();
				state = 1;
			}
			errled_h = timeout(timeout_errled, NULL, hz/5);
		} else {
			do 
				count++;
			while (errled[count] != '\0' &&
			    (errled[count] < '0' || errled[count] > '9'));
			if (errled[count] == '\0') {
				count = 0;
				errled_h = timeout(timeout_errled, NULL, hz * 2);
			} else {
				cnt2 = errled[count] - '0';
				state = 0;
				errled_h = timeout(timeout_errled, NULL, hz);
			}
		}
	}
	mtx_unlock(&errled_mtx);
	return;
}

/*
 * The write function is used for the error-LED.
 */

static int
elan_write(dev_t dev, struct uio *uio, int ioflag)
{
	int error;
	char *s, *q;

	if (minor(dev) != ELAN_ERRLED)
		return (EOPNOTSUPP);

	if (uio->uio_resid > 512)
		return (EINVAL);
	s = malloc(uio->uio_resid + 1, M_DEVBUF, M_WAITOK);
	if (s == NULL)
		return (ENOMEM);
	untimeout(timeout_errled, NULL, errled_h);
	s[uio->uio_resid] = '\0';
	error = uiomove(s, uio->uio_resid, uio);
	if (error) {
		free(s, M_DEVBUF);
		return (error);
	}
	mtx_lock(&errled_mtx);
	q = errled;
	errled = s;
	mtx_unlock(&errled_mtx);
	if (q != NULL)
		free(q, M_DEVBUF);
	timeout_errled(errled);

	return(0);
}

static int
elan_mmap(dev_t dev, vm_offset_t offset, int nprot)
{

	if (minor(dev) != ELAN_MMCR)
		return (EOPNOTSUPP);
	if (offset >= 0x1000) 
		return (-1);
	return (i386_btop(0xfffef000));
}

static int
elan_ioctl(dev_t dev, u_long cmd, caddr_t arg, int flag, struct  thread *tdr)
{
	return(ENOENT);
}

