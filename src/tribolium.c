/**
 * @file tribolium.c
 * @author Carl Boettiger, <cboettig@gmail.com>
 * @section DESCRIPTION
 * This is the actual population dynamics simulation.  Currently 
 * completely contained in this file except for the dependence on
 * my linked list library.  Implements an individual-based, Gillespie
 * simulation which captures demographic stochasticity in continuous 
 * time through four different age classes (egg, larvae, pupae, adult).
 * 
 * @section LICENCE
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 */



#include <stdio.h>
#include <math.h>
#include <time.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include "llist.h"


/*
 * Events:
 * -- random times --
 * death of an egg
 * death of larva
 * death of pupa
 * death of adult
 * birth of egg
 *
 *  -- fixed times --
 * transition egg to larva
 * transition larva to pupa
 * transition pupa to adult
 *
 * compare time to next event to time to next maturation
 */

void simulate(int * state, double * pars, double * dt, int * seed);

int main(void){
	const double b = 5;													/* Birth rate (per day) */
	const double u_egg = 0.0, 
				 u_larva = 0.001,
				 u_pupa = 0.0,
				 u_adult = 0.003; /* Mortality rate (per day) */
	const double a_egg = 3.8,
				 a_larva = 3.8+16.4,
				 a_pupa = 3.8+16.4+5.0;	/* age at which each stage matures, in days */
	const double cannibal_larva_eggs = 0.01,
				 cannibal_adults_pupa = 0.004,
				 cannibal_adults_eggs =  0.01; /* cannibalism of x on y, per day */
	const double a_larva_asym = 3.8+8.0;	  /* age after which larval size asymptotes */
	double pars[12] = {	b, u_egg, u_larva, u_pupa, u_adult, a_egg,
						a_larva, a_pupa, cannibal_larva_eggs,
						cannibal_adults_pupa, cannibal_adults_eggs,
						a_larva_asym};
	int state[5] = {100, 10, 10, 10, 10};
	int seed = time(NULL);
	double dt = 9;

	simulate(state, pars,  &dt, &seed);

	//printf("%lf %d %d %d %d %d\n", dt, state[0], state[1], state[2], state[3], state[4]);
	return 0;
}

/** Simulate */
void simulate(int * state, double * pars, double * dt, int * seed){
	/* Initialize random number generator with random seed */
	gsl_rng * rng = gsl_rng_alloc( gsl_rng_default);
	gsl_rng_set(rng, *seed);


	/** The population will live in a linked list named pop.  
	 * Every individual is a node.  Currently an individual is completely
	 * defined by its age alone, which determines its age class */
	LLIST *pop_head = NULL;
	LLIST **pop = &pop_head;

	/* Biological Parameters.  eggs and pupa can get canibalized */
	double b = pars[0];											/* Birth rate (per day) */
	double u_egg = pars[1], u_larva = pars[2], u_pupa = pars[3], u_adult = pars[4]; /* Mortality rate (per day) */
	double a_egg = pars[5], a_larva = pars[6], a_pupa = pars[7];	/* age at which each stage matures, in days */
	double cannibal_larva_eggs = pars[8], cannibal_adults_pupa = pars[9], cannibal_adults_eggs = pars[10]; /* cannibalism of x on y, per day */
	double a_larva_asym = pars[11];									/* age after which larval size asymptotes */


	/* Gillespie simulation parameters */
	double t=0, sampletime = *dt, timestep = 0;					/* overall time, sample timer, random time increment */
	int event;
	double tmp;
	double rates[5], cumrates[5], sumrates = 0;

	/* some additional parameters we keep track of */
	double immature_larva_weights;
	double next_mature_time;
	/* We'll use pointers to grab member of the right class to experience mortality.  */
	LLIST **egg = NULL;
	LLIST **immature_larva = NULL;
	LLIST **larva = NULL;
	LLIST **pupa = NULL;
	LLIST **adult = NULL;
	LLIST *p;


	/* initialize No individuals */ 
	int i;
	for(i=0;i<state[0];i++) list_add(pop, 0.0);
	for(i=0;i<state[1];i++)	list_add(pop, a_egg);
	for(i=0;i<state[2];i++)	list_add(pop, a_larva_asym);
	for(i=0;i<state[3];i++) list_add(pop, a_larva);
	for(i=0;i<state[4];i++)	list_add(pop, a_pupa);


	while(t < 20.0){
		int age_classes[5] = {0,0,0,0,0};
		immature_larva_weights = 0;
		next_mature_time = a_pupa;
		p = *pop;
		/** Tour the population.  Keep track of:
		 * (a) number falling into each age bracket 
		 * (b) if any will mature before the next birth/death event
		 * (c) get a pointer to a member of each class, so we can grab
		 *     a member of the appropriate class when we have to kill it 
		 *
		 *	immature larva have a weight determined by their age, so we'll calculate that too
		 *     */
		while(p != NULL) {
			p->data += timestep;
			if(p->data < a_egg) { 
				++age_classes[0];												
				next_mature_time = GSL_MIN_DBL(next_mature_time, a_egg - p->data);
				egg = &p;															
			} else if(p->data < a_larva_asym) {
				++age_classes[1];
				immature_larva_weights += (p->data - a_egg)/(a_larva_asym-a_egg);
				immature_larva = &p;
			} else if(p->data < a_larva) {
				++age_classes[2]; 
				next_mature_time = GSL_MIN_DBL(next_mature_time, a_larva - p->data);
				larva = &p;
			} else if(p->data < a_pupa) { 
				++age_classes[3]; 
				next_mature_time = GSL_MIN_DBL(next_mature_time, a_pupa - p->data);
				pupa = &p;
			} else {  
				++age_classes[4];
				adult = &p;
				}
			p = p->next;
		}

		/* Create the Gillespie vector of rates, in this order: 
		 * egg death, larva death, pupa death, adult death, birth 
		 * Then determine which event happens and when */
		rates[0] = age_classes[0] * (u_egg + ((double) age_classes[2] + immature_larva_weights) * cannibal_larva_eggs + age_classes[4]*cannibal_adults_eggs );
		rates[1] = (age_classes[2]+age_classes[1])*u_larva;
		rates[2] = age_classes[3]* (u_pupa + age_classes[4]*cannibal_adults_pupa );
		rates[3] = age_classes[4]*u_adult;
		rates[4] = age_classes[4]*b;

		sumrates=0;
		for(i=0;i<5;i++){
			sumrates += rates[i];
			cumrates[i] = sumrates;
		}

		timestep = gsl_ran_exponential(rng, 1/sumrates);
		t += timestep;
		event=0;

		/* Sample the data at regular intervals */
		if(t > sampletime){
			/* update state, could be done more elegantly with vector views */
			state[0] = age_classes[0];	state[1] = age_classes[1]; state[2] = age_classes[2]; 
			state[3] = age_classes[3]; state[4] = age_classes[4];
			printf("%lf %d %d %d %d %d\n", sampletime, state[0], state[1], state[2], state[3], state[4]);
			sampletime += *dt;
		}

		/* If no one is maturing before next event, pick a random birth/death */
		if(timestep < next_mature_time){
			tmp = gsl_rng_uniform(rng);
			for(i=0;i<5;i++){
				if (tmp < cumrates[i]/sumrates){
					event = i;
					break;
				}
			}

			switch(event)
			{
				case 0 : list_remove(egg); break;
				case 1 : 
					if( gsl_rng_uniform(rng) < age_classes[1]/(age_classes[1]+age_classes[2]) ){
						list_remove(immature_larva); 
					} else { 
						list_remove(larva); 
					}
					break; 	
				case 2 : list_remove(pupa); break;
				case 3 : list_remove(adult); break;
				case 4 : list_add(pop, 0.0); break; 
			}
		}
	} // loop over time
	list_free(*pop);
	gsl_rng_free(rng);
}





