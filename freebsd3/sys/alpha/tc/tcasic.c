/* $Id: tcasic.c,v 1.1 1998/08/20 08:27:11 dfr Exp $ */
/* from $NetBSD: tcasic.c,v 1.23 1998/05/14 00:01:31 thorpej Exp $ */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
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

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/rpb.h>

/*#include <alpha/tc/dwlpxreg.h>*/

#define KV(pa)			ALPHA_PHYS_TO_K0SEG(pa)

static devclass_t	tcasic_devclass;
static device_t		tcasic0;		/* XXX only one for now */

struct tcasic_softc {
	vm_offset_t	dmem_base;	/* dense memory */
	vm_offset_t	smem_base;	/* sparse memory */
	vm_offset_t	io_base;	/* sparse i/o */
	vm_offset_t	cfg_base;	/* sparse pci config */
};

#define TCASIC_SOFTC(dev)	(struct tcasic_softc*) device_get_softc(dev)

static int tcasic_probe(device_t dev);
static int tcasic_attach(device_t dev);
static void tcasic_print_child(device_t bus, device_t dev);

static device_method_t tcasic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tcasic_probe),
	DEVMETHOD(device_attach,	tcasic_attach),
	DEVMETHOD(bus_print_child,      tcasic_print_child),
	{ 0, 0 }
};

static driver_t tcasic_driver = {
	"tcasic",
	tcasic_methods,
	DRIVER_TYPE_MISC,
	sizeof(struct tcasic_softc),
};

extern device_t tc0;
static int
tcasic_probe(device_t dev)
{
	if (tcasic0)
		return ENXIO;
	if((hwrpb->rpb_type != ST_DEC_3000_300) &&
	   (hwrpb->rpb_type != ST_DEC_3000_500))
		return ENXIO;
	tcasic0 = dev;
	device_set_desc(dev, "Turbochannel Host Bus Adapter");
	tc0 = device_add_child(dev, "tc", 0, 0);
	return 0;
}

static int
tcasic_attach(device_t dev)
{
	struct tcasic_softc* sc = TCASIC_SOFTC(dev);
	device_t parent = device_get_parent(dev);
	vm_offset_t regs;
	tcasic0 = dev;

/*	chipset = tcasic_chipset;*/
	chipset.intrdev = dev;
	device_probe_and_attach(tc0);
	return 0;
}

static void
tcasic_print_child(device_t bus, device_t dev)
{
        printf(" at %s%d",
               device_get_name(bus), device_get_unit(bus));
}



DRIVER_MODULE(tcasic, root, tcasic_driver, tcasic_devclass, 0, 0);

