/* $NetBSD: dec_3000_300.c,v 1.25.2.1 1999/04/16 23:10:35 thorpej Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Additional Copyright (c) 1997 by Matthew Jacob for NASA/Ames Research Center
 */

#include <sys/cdefs.h>			/* RCS ID & Copyright macro defns */

__KERNEL_RCSID(0, "$NetBSD: dec_3000_300.c,v 1.25.2.1 1999/04/16 23:10:35 thorpej Exp $");

#include "opt_new_scc_driver.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/termios.h>

#include <machine/rpb.h>
#include <machine/autoconf.h>
#include <machine/conf.h>

#include <dev/tc/tcvar.h>
#include <alpha/tc/tcdsvar.h>
#include <alpha/tc/tc_3000_300.h>

#include <machine/z8530var.h>
#include <dev/dec/zskbdvar.h>

#include <dev/scsipi/scsi_all.h>
#include <dev/scsipi/scsipi_all.h>
#include <dev/scsipi/scsiconf.h>

#include "wsdisplay.h"

void dec_3000_300_init __P((void));
static void dec_3000_300_cons_init __P((void));
static void dec_3000_300_device_register __P((struct device *, void *));

const struct alpha_variation_table dec_3000_300_variations[] = {
	{ SV_ST_PELICAN, "DEC 3000/300 (\"Pelican\")" },
	{ SV_ST_PELICA, "DEC 3000/300L (\"Pelica\")" },
	{ SV_ST_PELICANPLUS, "DEC 3000/300X (\"Pelican+\")" },
	{ SV_ST_PELICAPLUS, "DEC 3000/300LX (\"Pelica+\")" },
	{ 0, NULL },
};

void
dec_3000_300_init()
{
	u_int64_t variation;

	platform.family = "DEC 3000/300 (\"Pelican\")";

	if ((platform.model = alpha_dsr_sysname()) == NULL) {
		variation = hwrpb->rpb_variation & SV_ST_MASK;
		if ((platform.model = alpha_variation_name(variation,
		    dec_3000_300_variations)) == NULL)
			platform.model = alpha_unknown_sysname();
	}

	platform.iobus = "tcasic";
	platform.cons_init = dec_3000_300_cons_init;
	platform.device_register = dec_3000_300_device_register;
}

static void
dec_3000_300_cons_init()
{
#if defined(NEW_SCC_DRIVER)
	struct ctb *ctb;

	ctb = (struct ctb *)(((caddr_t)hwrpb) + hwrpb->rpb_ctb_off);

	switch (ctb->ctb_term_type) {
	case CTB_GRAPHICS:
#if NWSDISPLAY > 0
		/* display console ... */
		if (zs_ioasic_lk201_cnattach(0x1a0000000, 0x00180000, 0) &&
		    tc_3000_300_fb_cnattach(
		     CTB_TURBOSLOT_SLOT(ctb->ctb_turboslot))) {
			break;
		}
#endif
		printf("consinit: Unable to init console on keyboard and ");
		printf("TURBOchannel slot 0x%lx.\n", ctb->ctb_turboslot);
		printf("Using serial console.\n");
		/* FALLTHROUGH */

	case CTB_PRINTERPORT:
		/* serial console ... */
		/*
		 * XXX This could stand some cleanup...
		 */
		{
			/*
			 * Delay to allow PROM putchars to complete.
			 * FIFO depth * character time.
			 * character time = (1000000 / (defaultrate / 10))
			 */
			DELAY(160000000 / 9600);	/* XXX */

			/*
			 * Console is channel B of the first SCC.
			 * XXX Should use ctb_line_off to get the
			 * XXX line parameters.
			 */
			if (zs_ioasic_cnattach(0x1a0000000, 0x00100000, 1,
			    9600, (TTYDEF_CFLAG & ~(CSIZE | PARENB)) | CS8))
				panic("can't init serial console");
			break;
		}

	default:
		printf("ctb->ctb_term_type = 0x%lx\n", ctb->ctb_term_type);
		printf("ctb->ctb_turboslot = 0x%lx\n", ctb->ctb_turboslot);

		panic("consinit: unknown console type %lu\n",
		    ctb->ctb_term_type);
	}
#endif /* NEW_SCC_DRIVER */
}

static void
dec_3000_300_device_register(dev, aux)
	struct device *dev;
	void *aux;
{
	static int found, initted, scsiboot, netboot;
	static struct device *scsidev;
	struct bootdev_data *b = bootdev_data;
	struct device *parent = dev->dv_parent;
	struct cfdata *cf = dev->dv_cfdata;
	struct cfdriver *cd = cf->cf_driver;

	if (found)
		return;

	if (!initted) {
		scsiboot = (strcmp(b->protocol, "SCSI") == 0);
		netboot = (strcmp(b->protocol, "BOOTP") == 0);
#if 0
		printf("scsiboot = %d, netboot = %d\n", scsiboot, netboot);
#endif
		initted =1;
	}

	if (scsiboot && (strcmp(cd->cd_name, "asc") == 0)) {
		if (b->slot == 4 &&
		    strcmp(parent->dv_cfdata->cf_driver->cd_name, "tcds")
		      == 0) {
			struct tcdsdev_attach_args *tcdsdev = aux;

			if (tcdsdev->tcdsda_chip == b->channel) {
				scsidev = dev;
#if 0
				printf("\nscsidev = %s\n", dev->dv_xname);
#endif
			}
		}
	}

	if (scsiboot &&
	    (strcmp(cd->cd_name, "sd") == 0 ||
	     strcmp(cd->cd_name, "st") == 0 ||
	     strcmp(cd->cd_name, "cd") == 0)) {
		struct scsipibus_attach_args *sa = aux;

		if (scsidev == NULL)
			return;

		if (parent->dv_parent != scsidev)
			return;

		if (b->unit / 100 != sa->sa_sc_link->scsipi_scsi.target)
			return;

		/* XXX LUN! */

		switch (b->boot_dev_type) {
		case 0:
			if (strcmp(cd->cd_name, "sd") &&
			    strcmp(cd->cd_name, "cd"))
				return;
			break;
		case 1:
			if (strcmp(cd->cd_name, "st"))
				return;
			break;
		default:
			return;
		}

		/* we've found it! */
		booted_device = dev;
#if 0
		printf("\nbooted_device = %s\n", booted_device->dv_xname);
#endif
		found = 1;
	}

	if (netboot) {
                if (b->slot == 5 && strcmp(cd->cd_name, "le") == 0 &&
		    strcmp(parent->dv_cfdata->cf_driver->cd_name, "ioasic")
		     == 0) {
			/*
			 * no need to check ioasic_attach_args, since only
			 * one le on ioasic.
			 */

			booted_device = dev;
#if 0
			printf("\nbooted_device = %s\n", booted_device->dv_xname);
#endif
			found = 1;
			return;
		}

		/*
		 * XXX GENERIC SUPPORT FOR TC NETWORK BOARDS
		 */
        }
}
