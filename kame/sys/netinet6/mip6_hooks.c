/*	$KAME: mip6_hooks.c,v 1.3 2000/02/22 14:04:25 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 and 2000 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999 and 2000 Ericsson Radio Systems AB
 * All rights reserved.
 *
 * Author: Hesham Soliman <hesham.soliman@ericsson.com.au>
 *         Martti Kuparinen <martti.kuparinen@ericsson.com>
 *
 * $Id: mip6_hooks.c,v 1.3 2000/02/22 14:04:25 itojun Exp $
 *
 */
#if (defined(__FreeBSD__) && __FreeBSD__ >= 3) || defined(__NetBSD__)
#include "opt_inet.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/mip6.h>
#include <netinet6/mip6_common.h>

/*
 * These are defined in /sys/netinet6/
 */
extern int (*mip6_store_dstopt_pre_hook)(struct mbuf *, u_int8_t *,
                                         u_int8_t, u_int8_t);
extern int (*mip6_rec_ctrl_sig_hook)(struct mbuf *, int);
extern int (*mip6_new_packet_hook)(struct mbuf *);
extern int (*mip6_icmp6_input_hook)(struct mbuf *, int);
extern int (*mip6_output_hook)(struct mbuf *, struct ip6_pktopts **);
extern int (*mip6_rec_ra_hook)(struct mbuf *, int);

/* Home Agent-specific hooks */
extern struct in6_addr * (*mip6_global_addr_hook)(struct in6_addr *);
extern struct mip6_subopt_hal * (*mip6_hal_dynamic_hook)(struct in6_addr *);
extern int  (*mip6_write_config_data_ha_hook)(u_long, void *);
extern int  (*mip6_clear_config_data_ha_hook)(u_long, void *);
extern int  (*mip6_enable_func_ha_hook)(u_long, caddr_t);
extern void (*mip6_icmp6_output_hook)(struct mbuf *);

/* Mobile Node-specific hooks */
extern int  (*mip6_route_optimize_hook)(struct mbuf *);
extern void (*mip6_select_defrtr_hook)(void);
extern struct nd_prefix * (*mip6_get_home_prefix_hook)(void);
extern void (*mip6_prelist_update_hook)(struct nd_prefix *,
                                        struct nd_defrouter *);
extern void (*mip6_expired_defrouter_hook)(struct nd_defrouter *);
extern void (*mip6_probe_pfxrtrs_hook)(void);
extern void (*mip6_store_advint_hook)(struct nd_opt_advint *,
                                      struct nd_defrouter *);
extern int  (*mip6_get_md_state_hook)(void);
extern int  (*mip6_rec_ba_hook)(struct mbuf *, int);
extern int  (*mip6_rec_br_hook)(struct mbuf *, int);
extern void (*mip6_stop_bu_hook)(struct in6_addr *);
extern int  (*mip6_write_config_data_mn_hook)(u_long, void *);
extern int  (*mip6_clear_config_data_mn_hook)(u_long, caddr_t);
extern int  (*mip6_enable_func_mn_hook)(u_long, caddr_t);
extern void (*mip6_minus_a_case_hook)(struct nd_prefix *);
extern struct mip6_esm * (*mip6_esm_find_hook)(struct in6_addr *);


#if (defined(MIP6_MN) || defined(MIP6_MODULES))    
void
mip6_minus_a_case(struct nd_prefix *pr)
{
	struct in6_addr   addr;
	
    if (IN6_IS_ADDR_UNSPECIFIED(&pr->ndpr_addr) ||
        IN6_IS_ADDR_MULTICAST(&pr->ndpr_addr) ||
        IN6_IS_ADDR_LINKLOCAL(&pr->ndpr_addr)) {
        return;
    }

	addr = in6addr_any;
    mip6_esm_create(pr->ndpr_ifp, NULL, &addr, &pr->ndpr_addr,
		    pr->ndpr_plen, MIP6_STATE_UNDEF, PERMANENT, 0xFFFF);
#ifdef MIP6_DEBUG
    mip6_debug("Late Home Address %s found for autoconfig'd case. Starting Mobile "
          "IPv6. Hook deactivated.\n", ip6_sprintf(&pr->ndpr_addr));
#endif
    mip6_minus_a_case_hook = 0;
    mip6_enable_hooks(MIP6_SPECIFIC_HOOKS);
    mip6_md_init();
}
#endif

#if (defined(MIP6_MN) || defined(MIP6_MODULES))
struct nd_prefix *
mip6_find_auto_home_addr(void)
{
    struct nd_prefix *pr;

    for (pr = nd_prefix.lh_first; pr; pr = pr->ndpr_next) {
        struct in6_ifaddr *ia6;

        if (IN6_IS_ADDR_UNSPECIFIED(&pr->ndpr_addr) ||
            IN6_IS_ADDR_MULTICAST(&pr->ndpr_addr) ||
            IN6_IS_ADDR_LINKLOCAL(&pr->ndpr_addr)) {
            continue;
        }
        ia6 = in6ifa_ifpwithaddr(pr->ndpr_ifp, &pr->ndpr_addr);
        if (ia6->ia6_flags || IN6_IFF_DETACHED)
            continue;
        else
            break;  /* XXXYYY Remove in v2.0. */
        
        /* XXXYYY Add in v2.0
           for (pfxrtr = pr->ndpr_advrtrs.lh_first; pfxrtr; 
             pfxrtr = pfxrtr->pfr_next) {
            if ((pfxrtr->router->flags & ND_RA_FLAG_HA) == ND_RA_FLAG_HA)
                break;
                }*/
    }
    if (pr) {
#ifdef MIP6_DEBUG
        mip6_debug("Found an autoconfig'd home address immediately: %s\n",
                   ip6_sprintf(&pr->ndpr_addr));
#endif
    }
    
    return pr;
}
#endif


void
mip6_enable_hooks(int scope)
{
    int  s;

    /*
     * Activate the hook functions. After this some packets might come
     * to the module...
     * Note: mip6_minus_a_case_hook() is an exception and is not handled here.
     */
    s = splimp();
    if (scope == MIP6_GENERIC_HOOKS) {
        mip6_store_dstopt_pre_hook = mip6_store_dstopt_pre;
        mip6_rec_ctrl_sig_hook = mip6_rec_ctrl_sig;
        mip6_new_packet_hook = mip6_new_packet;
        mip6_icmp6_input_hook = mip6_icmp6_input;
        mip6_output_hook = mip6_output;
    }

    if (scope == MIP6_CONFIG_HOOKS) {
        /* Activate Home Agent-specific hooks */
#if (defined(MIP6_HA) || defined(MIP6_MODULES))    
        if (MIP6_IS_HA_ACTIVE) {
            mip6_write_config_data_ha_hook = mip6_write_config_data_ha;
            mip6_clear_config_data_ha_hook = mip6_clear_config_data_ha;
            mip6_enable_func_ha_hook = mip6_enable_func_ha;
        }
#endif
        
        /* Activate Mobile Node-specific hooks */
#if (defined(MIP6_MN) || defined(MIP6_MODULES))    
        if (MIP6_IS_MN_ACTIVE) {
            mip6_write_config_data_mn_hook = mip6_write_config_data_mn;
            mip6_clear_config_data_mn_hook = mip6_clear_config_data_mn;
            mip6_enable_func_mn_hook = mip6_enable_func_mn;
        }
#endif
    }

    if (scope == MIP6_SPECIFIC_HOOKS) {

        /* Activate Home Agent-specific hooks */
#if (defined(MIP6_HA) || defined(MIP6_MODULES))    
        if (MIP6_IS_HA_ACTIVE) {
            mip6_rec_ra_hook = mip6_rec_raha;
            mip6_global_addr_hook = mip6_global_addr;
            mip6_hal_dynamic_hook = mip6_hal_dynamic;
            mip6_icmp6_output_hook = mip6_icmp6_output;
        }
#endif
        
        /* Activate Mobile Node-specific hooks */
#if (defined(MIP6_MN) || defined(MIP6_MODULES))    
        if (MIP6_IS_MN_ACTIVE) {
            mip6_route_optimize_hook = mip6_route_optimize;
            mip6_rec_ra_hook = mip6_rec_ramn;   
            mip6_select_defrtr_hook = mip6_select_defrtr;
            mip6_get_home_prefix_hook = mip6_get_home_prefix;
            mip6_prelist_update_hook = mip6_prelist_update;
            mip6_expired_defrouter_hook = mip6_expired_defrouter;
            mip6_probe_pfxrtrs_hook = mip6_probe_pfxrtrs;
            mip6_store_advint_hook = mip6_store_advint;
            mip6_get_md_state_hook = mip6_get_md_state;
            mip6_rec_ba_hook = mip6_rec_ba;
            mip6_rec_br_hook = mip6_rec_bu;
            mip6_stop_bu_hook = mip6_stop_bu;
            mip6_esm_find_hook = mip6_esm_find;
        }
#endif
    }
    splx(s);
    return;
}


void
mip6_disable_hooks(int scope)
{
    int  s;

    /*
     * Deactivate the hook functions. After this some packets might not come
     * to the module...
     */
    s = splimp();

    if (scope == MIP6_GENERIC_HOOKS) {
        mip6_store_dstopt_pre_hook = 0;
        mip6_rec_ctrl_sig_hook = 0;
        mip6_new_packet_hook = 0;
        mip6_icmp6_input_hook = 0;
        mip6_output_hook = 0;
    }

    if (scope == MIP6_SPECIFIC_HOOKS) {
        
            /* De-activate Home Agent-specific hooks */
#if (defined(MIP6_HA) || defined(MIP6_MODULES))    
        if (MIP6_IS_HA_ACTIVE) {
            mip6_rec_ra_hook = 0;
            mip6_global_addr_hook = 0;
            mip6_hal_dynamic_hook = 0;
            mip6_write_config_data_ha_hook = 0;
            mip6_clear_config_data_ha_hook = 0;
            mip6_enable_func_ha_hook = 0;
        }
#endif
        
            /* De-activate Mobile Node-specific hooks */
#if (defined(MIP6_MN) || defined(MIP6_MODULES))    
        if (MIP6_IS_MN_ACTIVE) {
            mip6_route_optimize_hook = 0;
            mip6_rec_ra_hook = 0;
            mip6_select_defrtr_hook = 0;
            mip6_get_home_prefix_hook = 0;
            mip6_prelist_update_hook = 0;
            mip6_expired_defrouter_hook = 0;
            mip6_probe_pfxrtrs_hook = 0;
            mip6_store_advint_hook = 0;
            mip6_get_md_state_hook = 0;
            mip6_rec_ba_hook = 0;
            mip6_rec_br_hook = 0;
            mip6_stop_bu_hook = 0;
            mip6_write_config_data_mn_hook = 0;
            mip6_clear_config_data_mn_hook = 0;
            mip6_enable_func_mn_hook = 0;
            mip6_esm_find_hook = 0;
            mip6_minus_a_case_hook = 0;
        }
#endif  
    }
    splx(s);
    return;
}



int
mip6_attach(void)
{
/*
  No support for modules here yet.  XXXYYY
  
  #if (defined(MIP6_MN) || defined (MIP6_HA) || defined(MIP6_MODULES))
*/
#ifdef MIP6_MN
    if (MIP6_IS_MN_ACTIVE) {
        if(mip6_get_home_prefix_hook)       /* Test arbitrary hook */
            return 0;
        
        /* 
           If autoconfig state: find a global address to use as Home Address.
           - Take first available on any interface, else if no found:
           - Enable hook to wait for a Router Advertisement to give us one.
        */
        if (mip6_config.autoconfig) {
            struct nd_prefix *pr;
            struct in6_addr   addr;
            
            addr = in6addr_any;
            if ((pr = mip6_find_auto_home_addr()) != NULL) {
                mip6_esm_create(pr->ndpr_ifp, &addr, &addr,
                                &pr->ndpr_addr,pr->ndpr_plen,
                                MIP6_STATE_UNDEF, PERMANENT, 0xFFFF);
                mip6_enable_hooks(MIP6_SPECIFIC_HOOKS);
                mip6_md_init();
            }
            else
                mip6_minus_a_case_hook = mip6_minus_a_case;
        }
        else {
            /* Manual config */
            mip6_enable_hooks(MIP6_SPECIFIC_HOOKS);
            mip6_md_init();
        }
    }
#endif
#ifdef MIP6_HA
    if (MIP6_IS_HA_ACTIVE) {
        /* XXXYYY Build anycast or is it done? */
        mip6_enable_hooks(MIP6_SPECIFIC_HOOKS);
    }
#endif
    return 0;
}



int
mip6_release(void)
{
    /* Disable the hooks */
    mip6_disable_hooks(MIP6_SPECIFIC_HOOKS);

#ifdef MIP6_MN
    if (MIP6_IS_MN_ACTIVE) {
        mip6_mn_exit();
        mip6_md_exit();
    }
#endif

#ifdef MIP6_HA
    if (MIP6_IS_HA_ACTIVE)
        mip6_ha_exit();
#endif

/*
  Correspondent Node functionality is never terminated.
  mip6_disable_hooks(MIP6_GENERIC_HOOKS);
  mip6_exit();
*/
    return 0;
}
