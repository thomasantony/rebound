/**
 * @file 	integrator_wkm.c
 * @brief   Wisdom Kernel Method 
 * @author  Hanno Rein <hanno@hanno-rein.de>
 * @details This file implements the Kernel method of Wisdom, 
 * Holman, and Touma (1996)
 * 
 * @section LICENSE
 * Copyright (c) 2019 Hanno Rein
 *
 * This file is part of rebound.
 *
 * rebound is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rebound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include "rebound.h"
#include "particle.h"
#include "tools.h"
#include "gravity.h"
#include "integrator.h"
#include "integrator_whfast.h"
#include "integrator_wkm.h"

#define MAX(a, b) ((a) < (b) ? (b) : (a))   ///< Returns the maximum of a and b
#define MIN(a, b) ((a) > (b) ? (b) : (a))   ///< Returns the minimum of a and b

static void reb_wkm_corrector_Z(struct reb_simulation* r, const double a, const double b){
    struct reb_simulation_integrator_whfast* const ri_whfast = &(r->ri_whfast);
    struct reb_particle* restrict const particles = r->particles;
    const int N_real = r->N;
    reb_whfast_kepler_step(r, a);
    reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N_real);
    reb_update_acceleration(r);
    reb_whfast_interaction_step(r, -b);
    reb_whfast_kepler_step(r, -2.*a);
    reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N_real);
    reb_update_acceleration(r);
    reb_whfast_interaction_step(r, b);
    reb_whfast_kepler_step(r, a);
}
void reb_wkm_apply_C(struct reb_simulation* const r, double a, double b){
    reb_whfast_kepler_step(r, a);   
    reb_whfast_com_step(r, a);
    
    struct reb_simulation_integrator_whfast* const ri_whfast = &(r->ri_whfast);
    struct reb_particle* restrict const particles = r->particles;
    const int N_real = r->N;
    reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N_real);
    reb_update_acceleration(r);
    reb_whfast_interaction_step(r, b);
    
    reb_whfast_kepler_step(r, -a);   
    reb_whfast_com_step(r, -a);
}

void reb_wkm_apply_Y(struct reb_simulation* const r, double a, double b){
    reb_wkm_apply_C(r, a, b); 
    reb_wkm_apply_C(r, -a, -b); 
}
void reb_wkm_apply_U(struct reb_simulation* const r, double a, double b){
    reb_whfast_kepler_step(r, a);   
    reb_whfast_com_step(r, a);
    reb_wkm_apply_Y(r, a, b); 
    reb_wkm_apply_Y(r, a, -b); 
    reb_whfast_kepler_step(r, -a);   
    reb_whfast_com_step(r, -a);
}
void reb_wkm_apply_corrector2(struct reb_simulation* const r, double dt){
    double a = 0.5 * dt;
    double b = 0.03486083443891981449909050107438281205803 * dt;
    reb_wkm_apply_U(r, a, b); 
    reb_wkm_apply_U(r, -a, b);
}

void reb_integrator_wkm_part1(struct reb_simulation* const r){
    struct reb_simulation_integrator_whfast* const ri_whfast = &(r->ri_whfast);
    struct reb_simulation_integrator_wkm* const ri_wkm = &(r->ri_wkm);
    const int corrector = ri_wkm->corrector%10;
    const int kernel = ri_wkm->corrector/10;
    r->gravity_ignore_terms = 1;
    if (r->var_config_N>0 && ri_whfast->coordinates!=REB_WHFAST_COORDINATES_JACOBI){
        reb_error(r, "Variational particles are not supported in the WKM integrator.");
        return; 
    }
    if (ri_whfast->coordinates!=REB_WHFAST_COORDINATES_JACOBI){
        reb_error(r, "WKM integrator requires ri_whfast.coordinates to be set to Jacobi coordinates.");
        return; 
    }
    if (kernel>1){
        reb_error(r, "WKM Kernel not implemented");
        return;
    }
    if (reb_integrator_whfast_init(r)){
        // Non recoverable error occured.
        return;
    }
    
    // Only recalculate Jacobi coordinates if needed
    if (ri_wkm->safe_mode || ri_whfast->recalculate_coordinates_this_timestep){
        reb_integrator_whfast_from_inertial(r);
        ri_whfast->recalculate_coordinates_this_timestep = 0;
    }
    if (ri_wkm->is_synchronized){
        if (corrector){
            reb_whfast_apply_corrector(r, 1., 11, reb_wkm_corrector_Z);
            if (corrector>=2){
                reb_wkm_apply_corrector2(r, r->dt);
            }
        }
        if (kernel==0){ // composition
            reb_whfast_kepler_step(r, 5./8.*r->dt);   
            reb_whfast_com_step(r, 5./8.*r->dt);
        }else if(kernel==1){ // lazy implementers method
            reb_whfast_kepler_step(r, 1./2.*r->dt);   
            reb_whfast_com_step(r, 1./2.*r->dt);
        }
    }else{
        reb_whfast_kepler_step(r, r->dt);   
        reb_whfast_com_step(r, r->dt);
    }

    reb_integrator_whfast_to_inertial(r);
}

void reb_integrator_wkm_synchronize(struct reb_simulation* const r){
    struct reb_simulation_integrator_whfast* const ri_whfast = &(r->ri_whfast);
    struct reb_simulation_integrator_wkm* const ri_wkm = &(r->ri_wkm);
    const int corrector = ri_wkm->corrector%10;
    const int kernel = ri_wkm->corrector/10;
    if (ri_wkm->is_synchronized == 0){
        const int N = r->N;
        struct reb_particle* sync_pj  = NULL;
        r->gravity_ignore_terms = 1; // needed here in case of SimulationArchive calls
        if (ri_whfast->keep_unsynchronized){
            sync_pj = malloc(sizeof(struct reb_particle)*r->N);
            memcpy(sync_pj,r->ri_whfast.p_jh,r->N*sizeof(struct reb_particle));
        }
        if (kernel==0){ // composition
            reb_whfast_kepler_step(r, 3./8.*r->dt);
            reb_whfast_com_step(r, 3./8.*r->dt);
        }else if (kernel==1){ // lazy implementers method
            reb_whfast_kepler_step(r, 1./2.*r->dt);
            reb_whfast_com_step(r, 1./2.*r->dt);
        }

        if (corrector){
            reb_whfast_apply_corrector(r, -1., 11, reb_wkm_corrector_Z);
            if (corrector>=2){
                reb_wkm_apply_corrector2(r, -r->dt);
            }
        }
        reb_transformations_jacobi_to_inertial_posvel(r->particles, ri_whfast->p_jh, r->particles, N);
        if (ri_whfast->keep_unsynchronized){
            memcpy(r->ri_whfast.p_jh,sync_pj,r->N*sizeof(struct reb_particle));
            free(sync_pj);
        }else{
            ri_wkm->is_synchronized = 1;
        }
    }
}

void reb_integrator_wkm_part2(struct reb_simulation* const r){
    struct reb_simulation_integrator_whfast* const ri_whfast = &(r->ri_whfast);
    struct reb_simulation_integrator_wkm* const ri_wkm = &(r->ri_wkm);
    struct reb_particle* restrict const particles = r->particles;
    const int kernel = ri_wkm->corrector/10;
    const int N = r->N;
    if (ri_whfast->p_jh==NULL){
        // Non recoverable error occured earlier. 
        // Skipping rest of integration to avoid segmentation fault.
        return;
    }
    if (kernel==0){ // composition
        
        // -1/6 B
        reb_whfast_interaction_step(r, -1./6.*r->dt);
      
        { // -1/4 A
            reb_whfast_kepler_step(r, -1./4.*r->dt);   
            reb_whfast_com_step(r, -1./4.*r->dt);
        }
        { // 1/6 B
            reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N);
            reb_update_acceleration(r);
            reb_whfast_interaction_step(r, 1./6.*r->dt);
        }
        { // 1/8 A
            reb_whfast_kepler_step(r, 1./8.*r->dt);   
            reb_whfast_com_step(r, 1./8.*r->dt);
        }
        { // B
            reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N);
            reb_update_acceleration(r);
            reb_whfast_interaction_step(r, r->dt);
        }
        { // -1/8 A
            reb_whfast_kepler_step(r, -1./8.*r->dt);   
            reb_whfast_com_step(r, -1./8.*r->dt);
        }
        { // -1/6 B
            reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N);
            reb_update_acceleration(r);
            reb_whfast_interaction_step(r, -1./6.*r->dt);
        }
        { // 1/4 A
            reb_whfast_kepler_step(r, 1./4.*r->dt);   
            reb_whfast_com_step(r, 1./4.*r->dt);
        }
        { // 1/6 B
            reb_transformations_jacobi_to_inertial_pos(particles, ri_whfast->p_jh, particles, N);
            reb_update_acceleration(r);
            reb_whfast_interaction_step(r, 1./6.*r->dt);
        }
    } else if (kernel==1){ //lazy implementers method
        double dt = r->dt;
        struct reb_simulation_integrator_whfast* const ri_whfast = &(r->ri_whfast);
        struct reb_particle* const p_j = ri_whfast->p_jh;
        struct reb_particle* const particles = r->particles;
        struct reb_simulation_integrator_wkm* const ri_wkm = &(r->ri_wkm);
        const int N = r->N;
        if (ri_wkm->allocated_N != N){
            ri_wkm->allocated_N = N;
            ri_wkm->temp_pj = realloc(ri_wkm->temp_pj,sizeof(struct reb_particle)*N);
        }
        struct reb_particle* temp_pj = ri_wkm->temp_pj;

        const double G = r->G;

        // Calculate normal kick
        // Accelertions were already calculated before part2 gets called
        reb_transformations_inertial_to_jacobi_acc(r->particles, p_j, r->particles, N);

        // make copy of normal kick, also stores original positions
        memcpy(temp_pj,p_j,r->N*sizeof(struct reb_particle));

        // WHT Eq 10.6
        double eta = particles[0].m;
        for (unsigned int i=1;i<N;i++){
            const struct reb_particle pji = p_j[i];
            eta += pji.m;
            const double prefac1 = dt*dt/12.; 
            if (i>1){
                const double rj2i = 1./(pji.x*pji.x + pji.y*pji.y + pji.z*pji.z);
                const double rji  = sqrt(rj2i);
                const double rj3iM = rji*rj2i*G*eta;
                temp_pj[i].ax += rj3iM*temp_pj[i].x;
                temp_pj[i].ay += rj3iM*temp_pj[i].y;
                temp_pj[i].az += rj3iM*temp_pj[i].z;
            }
            p_j[i].x += prefac1 * temp_pj[i].ax;
            p_j[i].y += prefac1 * temp_pj[i].ay;
            p_j[i].z += prefac1 * temp_pj[i].az;
        }
       
        // recalculate kick 
        reb_transformations_jacobi_to_inertial_pos(particles, p_j, particles, N);
        reb_update_acceleration(r);
        reb_whfast_interaction_step(r, dt);

        for (unsigned int i=1;i<N;i++){
            // reset positions
            p_j[i].x = temp_pj[i].x;
            p_j[i].y = temp_pj[i].y;
            p_j[i].z = temp_pj[i].z;
        }
    }
    
    ri_wkm->is_synchronized = 0;
    if (ri_wkm->safe_mode){
        reb_integrator_wkm_synchronize(r);
    }
    
    r->t+=r->dt;
    r->dt_last_done = r->dt;
}
    
void reb_integrator_wkm_reset(struct reb_simulation* const r){
    struct reb_simulation_integrator_wkm* const ri_wkm = &(r->ri_wkm);
    ri_wkm->corrector = 1;
    ri_wkm->safe_mode = 1;
    ri_wkm->is_synchronized = 1;
    reb_integrator_whfast_reset(r);
    if (ri_wkm->temp_pj){
        free(ri_wkm->temp_pj);
        ri_wkm->temp_pj = NULL;
    }
    ri_wkm->allocated_N = 0;
}
