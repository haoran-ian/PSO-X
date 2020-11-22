/*
 * swarm.cpp
 *
 *  Created on: Jun 20, 2018
 *      Author: leonardo
 */
//cout << "\n So far, so good" << endl; //remove

#include "iostream"
#include "float.h"
#include "utils.h"
#include <bits/stdc++.h>
#include <new>
#include "problem.h"
#include "config.h"
#include "swarm.h"
#include "rng.h"

using namespace std;

/* Variables to compute the inertia weight using the available control strategies */
double alpha = 1/pow(M_PI,2); 				//small positive constant 		IW_NONL_DEC - 4
double omega = 0.3;						//value between [0,1] 			IW_NONL_DEC_IMP - 5
double u = 1.0002;							//value between [1.0001,1.0005] IW_NONL_DEC_IMP - 5
double omegaXu = omega * u;					//								IW_NONL_DEC_IMP - 5
double zFunction;							//								IW_CHAOTIC_DEC - 7
int	   k = 7;								//positive integer constant		IW_OSCILLATING - 9
double simNumOfCos = 2.0*M_PI*((4*k)+6);	//								IW_OSCILLATING - 9
double a = 1;								//small positive constant		IW_LOG_DEC - 10
double omega_2 = 0;							//								IW_SELF_REGULATING - 11
double idealVelocity;						//								IW_VELOCITY_BASED - 12
double avVel;								//								IW_VELOCITY_BASED - 12
int iterWithoutImprvCounter=0;
int iterWithImprvCounter=0;
double prevGbest = LDBL_MAX;
vector<SimplifySwarm> simpSwarm;
long partRemoved = 0;
long partAdded = 0;

/* Variable for the topology and model of influence */
vector<vector< int > > hierarchy;
vector<SimplifySwarm> rankedSwarm;			//ranked FI model of influence
int lastLevelComplete = 0;					//global variable for the hierarchy
bool modInfRanked = false;					//flag to indicate that the rankedSwarm structure was used and delete it

/* Variable for the perturbation strategies */
//int success = 15, failure = 5;				//success and failure thresholds for the additive rectangular perturbation
//int sc = 0, fc = 0;							//success and failure counters
//double alpha_t = 1.0;						//side length of the rectangle for the success-rate perturbation
//double delta = 1.0;							//side length of the rectangle for the uniform random perturbation
//double l = 0.01;							//scaling factor for the perturbation

bool sortcol(const vector<int>& v1, const vector<int>& v2) {
	return v1[1] < v2[1];
}

//Default constructor
Swarm::Swarm(){
	problem = 0;
	size = 0;
	best_particle = 0;
	init = false;
	ranked = false;
	hierarchical = false;
}

Swarm::~Swarm(){
	if (init) {
		// Memory allocated dynamically
		delete [] global_best.x;
		for (unsigned long int i=0;i<swarm.size();i++)
			delete swarm.at(i);
	}
	init=false;
}

Swarm::Swarm (Problem* problem, Configuration* config){
	this->problem = problem;
	size = config->getSwarmSize();

	/*Initialize global best*/
	global_best.x = new double[config->getProblemDimension()];
	for(unsigned int i=0;i<config->getProblemDimension();i++)
		global_best.x[i] = 0;
	global_best.eval=LDBL_MAX;

	if (config->verboseMode())
		cout << "\tvar::(default)::global_best.eval: " << global_best.eval << endl;

	for (long int i=0; i<size; i++) {
		Particle* aParticle = new Particle(problem, config, i, 0);
		swarm.push_back(aParticle);

		if (config->verboseMode())
			cout << "\tvar::swarm.at(" << i <<")->getPbestEvaluation(): " << swarm.at(i)->getPbestEvaluation() << endl;

		if (swarm.at(i)->getPbestEvaluation() < global_best.eval){
			updateGlobalBest(swarm.at(i)->getPbestPosition(), swarm.at(i)->getPbestEvaluation());
			best_particle = swarm.at(i);
		}
	}
	//	try{
	//best_particle->getID();
	if (best_particle != NULL){
		if (config->verboseMode()) cout << "\tvar::best_particle.id: " << best_particle->getID() << endl;

	}
	else {
		cerr << "The program could not determine an initial solution quality. " << endl;
		cerr << "HINT 1: Make sure that all input files can be accessed by the program" << endl;
		cerr << "HINT 2: This error is typically produced when the o.f. cannot be correctly evaluated" << endl;
		exit (-1);
	}
	if (config->verboseMode())
		cout << "\tvar::best_particle.id: " << best_particle->getID() << endl;
	//	}
	//	catch (const std::exception& e) {
	//		cerr << "The program could not determine an initial solution quality. " << endl;
	//		cerr << "HINT: Make sure that all input files can be accessed by the program" << endl;
	//		exit (-1);
	//	}
	hierarchical = false;

	//Select one of the available topologies
	if (config->getTopology() == TOP_FULLYCONNECTED) {
		createFullyConnectedTopology();
	} else if (config->getTopology() == TOP_HIERARCHICAL) {
		hierarchical = true;
		(config->getPopulationCS() == POP_CONSTANT) ? createHierarchical(config->getBranchingDegree(), swarm.size()) :
				createHierarchical(config->getBranchingDegree(), config->getFinalPopSize());
	} else if (config->getTopology() == TOP_RING) {
		createRingTopology();
	} else if (config->getTopology() == TOP_WHEEL) {
		createWheelTopology();
	} else if (config->getTopology() == TOP_RANDOM) {
		createRandomEdge();
	} else if (config->getTopology() == TOP_TIMEVARYING) {
		createFullyConnectedTopology();
		if (config->getPopulationCS() != POP_INCREMENTAL)
			config->setTopologyUpdatePeriod((int)floor((double)config->getTopologySchedule()/(config->getSwarmSize()-3)));
		else
			config->setTopologyUpdatePeriod((int)floor((double)config->getTopologySchedule()/(config->getFinalPopSize()-3)));
	} else if (config->getTopology() == TOP_VONNEUMANN) {
		createVonNeumannTopology();
	}
	else {
		cerr << "Wrong topology" << endl;
		exit (-1);
	}
	//	//Update gBest/lBest of each particle
	//	for (unsigned int i=0;i<swarm.size();i++){
	//		//The id of the gBest particle depends on the topology and model of influence
	//		swarm.at(i)->getBestOfNeibourhood();
	//	}

	//rankings
	if (	config->getOmega1CS() == IW_RANKS_BASED ||
			config->getOmega1CS() == IW_SUCCESS_BASED ||
			config->getOmega1CS() == IW_CONVERGE_BASED)
		ranked = true;

	init=true;
}

int Swarm::countImprovedSolutions(Configuration* config, long int iteration){
	//Create simpSwarm if it does not exist and return 0
	if (iteration == 1) { //define simpSwarm if it does not exist
		//if (config->getOmega1CS() != IW_RANKS_BASED && config->getOmega1CS() != IW_SUCCESS_BASED && config->getOmega1CS() != IW_CONVERGE_BASED){
		simpSwarm.clear();
		simpSwarm.resize(swarm.size());
		for (unsigned int i=0;i<swarm.size();i++){
			simpSwarm.at(i).id = swarm.at(i)->getID();
			simpSwarm.at(i).eval = swarm.at(i)->getCurrentEvaluation();
		}
		//}
		return 0;
	}
	else {
		int S_i = 0; //Number of solutions that improved after the last iteration
		for (unsigned int i=0; i<swarm.size(); i++){
			for (unsigned int j=0; j<simpSwarm.size(); j++){
				if (swarm.at(i)->getID() == simpSwarm.at(j).id){
					//evaluate if the solution improved
					if (swarm.at(i)->getCurrentEvaluation() < simpSwarm.at(j).eval){
						S_i++;
						break;
					}
					else
						break;
				}
			}
		}
		if (config->verboseMode())
			cout << "\tnotice::" <<  S_i << " solutions improved in the last iteration" << endl;
		return S_i;
	}
}


/*Move the swarm to new solutions */
void Swarm::moveSwarm(Configuration* config, long int iteration, const double minBound, const double maxBound) {

	//Move particles
	if (config->verboseMode())
		cout << "iteration: " << iteration << endl; //remove

	//Update gBest/lBest of each particle
	for (unsigned int i=0;i<swarm.size();i++){
		if (config->verboseMode())
			cout << "\tParticle [" << swarm.at(i)->getID() << "] -- lBest.at(t-1) [" << swarm.at(i)->getgBestID() << "] -- "; //<< endl; //remove
		//The id of the gBest particle depends on the topology and model of influence
		swarm.at(i)->getBestOfNeibourhood();
		if (config->verboseMode())
			cout << "lBest.at(t) [" << swarm.at(i)->getgBestID() << "]" << endl;
	}

	//Compute the number of solutions that improved in the last iteration
	int sol_improved = countImprovedSolutions(config, iteration);

	clearResizeSimpSwarm(config, iteration);

	if (config->verboseMode() && config->getTopology() == TOP_HIERARCHICAL){
		cout << "\tvar::lastLevelComplete: " << lastLevelComplete << endl;
		printTree(config->getBranchingDegree(),swarm.size());
	}

	//For the cases in which the entire swarm is using omega1 with the same value
	computeOmega1(config, iteration, -1, true); // -1 is a place holder for the id of the particle

	//We call this method here because some models of influence need to initialize things
	getInformants(config,-1,iteration);	// -1 is a place holder for the id of the particle

	//Compute the acceleration coefficients of the entire swarm
	computeAccelerationCoefficients(config, iteration);

	if (config->verboseMode()) cout << "\t------------------------------------------" << endl;
	for (unsigned int i=0;i<swarm.size();i++){
		if (config->verboseMode())
			cout << "\tParticle [" << swarm.at(i)->getID() << "] -- "; // -- gBestID [" << swarm.at(i)->getgBestID() << "] -- "; //<< endl; //remove
		int sizeInformants = getInformants(config, i, iteration); //Get the informants of i

		if (config->verboseMode()) {
			//print all neighbors
			cout << "\n\tNeighbors ids:  [ ";
			for (unsigned int j=0;j<swarm.at(i)->neighbours.size();j++){
				cout << swarm.at(i)->neighbours[j]->getID() << " ";
			}
			cout << "]" << endl;
			//print all neighbors
			cout << "\tInformants pos: [ ";
			for (int j=0;j<sizeInformants;j++){
				cout << swarm.at(i)->InformantsPos[j] << " ";
			}
			cout << "]" << endl;
			cout << "\tInformants ids: [ ";
			for (int j=0;j<sizeInformants;j++){
				cout << swarm.at(i)->neighbours[swarm.at(i)->InformantsPos[j]]->getID() << " ";
			}
			cout << "]"<< endl;
		}

		//If using a self-adaptive strategy compute omega1, otherwise this function simply returns the value already computed
		double omega1 = computeOmega1(config, iteration, i, false);

		//Note that here computeOmega1 receives the number of the particle and the flag = false
		swarm.at(i)->move(config, minBound, maxBound, iteration,
				omega1,
				computeOmega2(config),
				computeOmega3(config),
				sizeInformants,
				lastLevelComplete,
				sol_improved);
	}
	long double prev_Gbest_eval = global_best.eval; //best solution at iteration t-1

	//Update best_particle
	for (unsigned int i=0;i<swarm.size();i++){
		if (swarm.at(i)->getPbestEvaluation() < global_best.eval){
			//Update best_particle of the Swarm (gBest) -- not to be confused with the gBest of a particle, which depends on the topology and MoI
			Swarm::updateGlobalBest(swarm.at(i)->getPbestPosition(), swarm.at(i)->getPbestEvaluation());
			best_particle = swarm.at(i);
		}
	}

	if (config->useIndStrategies())
		//Reinitialize particle position and set v=0 if it is "too" similar to gBest
		reinitializeParticlePosition(config);

	updatePerturbationVariables(config, prev_Gbest_eval, global_best.eval, iteration);

}

int Swarm::getInformants(Configuration* config, int particleID, long int iteration){
	if (particleID != -1){
		//Best of neighborhood
		if (config->getModelOfInfluence() == MOI_BEST_OF_N){
			if (config->getTopology() == TOP_HIERARCHICAL){
				int Array_size = 0;		//variable to get the the size of Informants
				int TMP_Array[lastLevelComplete];
				getParticleParentsIDs(particleID, TMP_Array); //Get parents of the particle
				for (int i=0; i<=lastLevelComplete; i++){
					if (TMP_Array[i] != -2){ //-2 indicates an empty position
						Array_size++;
					}
					else
						break;
				}

				//Clear the vector
				swarm.at(particleID)->InformantsPos.clear();

				double bestEval;
				int bestPos;
				int iter = 0;
				//We need to find the position index of the best particle IDs in TMP_Array in the particles neighbors vector
				for (unsigned int i=0; i<swarm.at(particleID)->neighbours.size(); i++){
					for (int j=0; j<Array_size; j++){
						if (swarm.at(particleID)->neighbours.at(i)->getID() == TMP_Array[j]){
							if (iter == 0){
								bestPos = i;
								bestEval = swarm.at(particleID)->neighbours.at(i)->getPbestEvaluation();
								iter++;
							}
							else{
								if (bestEval > swarm.at(particleID)->neighbours.at(i)->getPbestEvaluation()){
									bestPos = i;
									bestEval = swarm.at(particleID)->neighbours.at(i)->getPbestEvaluation();
									iter++;
								}
							}
						}
					}
				}
				swarm.at(particleID)->InformantsPos.push_back(bestPos);//
				if (config->verboseMode()) cout << "Size of Informants: " << swarm.at(particleID)->InformantsPos.size();
				return 1;
			}
			else {
				//Clear the vector
				swarm.at(particleID)->InformantsPos.clear();

				int bestID = swarm.at(particleID)->getgBestID();

				//We need to find the position index of the IDs in TMP_Array in the particles neighbors vector
				for (unsigned int i=0; i<swarm.at(particleID)->neighbours.size(); i++){
					if (swarm.at(particleID)->neighbours.at(i)->getID() == bestID)
						swarm.at(particleID)->InformantsPos.push_back(i);
				}
				if (config->verboseMode()) cout << "Size of Informants: " << swarm.at(particleID)->InformantsPos.size();
				return 1;
			}
		}
		//Fully informed
		else if (config->getModelOfInfluence() == MOI_FI) {
			if (config->getTopology() == TOP_HIERARCHICAL ){
				int Array_size = 0;		//variable to the the size of Informants
				int TMP_Array[lastLevelComplete];
				getParticleParentsIDs(particleID, TMP_Array); //Get parents of the particle
				for (int i=0; i<=lastLevelComplete; i++){
					if (TMP_Array[i] != -2){ //-2 indicates an empty position
						Array_size++;
					}
					else
						break;
				}
				//Clear the vector
				swarm.at(particleID)->InformantsPos.clear();

				//We need to find the position index of the IDs in TMP_Array in the particles neighbors vector
				for (unsigned int i=0; i<swarm.at(particleID)->neighbours.size(); i++){
					for (int j=0; j<Array_size; j++)
						if (swarm.at(particleID)->neighbours.at(i)->getID() == TMP_Array[j])
							swarm.at(particleID)->InformantsPos.push_back(i);
				}
				if (config->verboseMode()) cout << "Size of Informants: " << swarm.at(particleID)->InformantsPos.size();
				return Array_size;
			}
			else {
				//Since some topologies are dynamic, the size of informants may change from iteration to iteration
				//Clear the vector
				swarm.at(particleID)->InformantsPos.clear();

				for (unsigned int i=0;i<swarm.at(particleID)->neighbours.size();i++){
					swarm.at(particleID)->InformantsPos.push_back(i); //we use the indexes of neighbors
				}
				if (config->verboseMode()) cout << "Size of Informants: " << swarm.at(particleID)->InformantsPos.size();
				return swarm.at(particleID)->InformantsPos.size();
			}
		}
		//Ranked fully informed
		else if (config->getModelOfInfluence() == MOI_RANKED_FI) {
			//Clear the vector
			swarm.at(particleID)->InformantsPos.clear();

			//Container to sort the neighbors
			vector< vector<int> > TMP_vect;
			//Resize vector
			TMP_vect.resize((swarm.at(particleID)->neighbours.size()), vector<int>(2));

			//This is the same as Fully informed
			for (unsigned int i=0;i<swarm.at(particleID)->neighbours.size();i++){
				TMP_vect.at(i).at(0) = i; ///we use the indexes of neighbors
				TMP_vect.at(i).at(1) = swarm.at(particleID)->neighbours.at(i)->getRanking();
			}

			//we sort by the second column (see sortcol driver function above)
			sort(TMP_vect.begin(), TMP_vect.end(), sortcol);

			//copy informants ID to Informants sorted
			for (unsigned int i=0; i<TMP_vect.size(); i++){ //rows
				swarm.at(particleID)->InformantsPos.push_back(TMP_vect[i][0]);
				//Informants[i] = TMP_vect[i][0];
				TMP_vect[i].clear();
			}
			TMP_vect.clear();

			if (config->verboseMode()) cout << "Size of Informants: " << swarm.at(particleID)->InformantsPos.size(); ;
			return swarm.at(particleID)->InformantsPos.size();;
		}
		else {
			cerr << "No model of influence matches the available options" << endl;
			exit (-1);
		}
	}
	else{
		//Ranked fully informed
		if (config->getModelOfInfluence() == MOI_RANKED_FI){
			//Only implement ranks if particles are not using them already
			if (config->getOmega1CS() != IW_RANKS_BASED){
				//				if ((iteration == 1) || (config->getPopulationCS() != POP_CONSTANT)){
				//					if (!modInfRanked) //flag to delete the structure at the end
				//						modInfRanked = true;
				rankedSwarm.clear();
				rankedSwarm.resize(swarm.size());
				//Rank particles
				rankParticles(rankedSwarm);
				//cout << "\nRanking swarm..." << endl;
				//cout << "\n So far, so good" << endl; //remove
				//				}
				//				else{
				//					rankParticles(rankedSwarm);
				//				}
			}
		}
		return 0;
	}
}

void Swarm::resizeSwarm(Problem* problem, Configuration* config, long int iteration){
	int particleToAdd = 0;
	long int previous_size = swarm.size();

	if (config->getPopulationCS() == POP_TIME_VARYING){
		//First time: Just save the global_best value
		if (iteration == 1){
			prevGbest = global_best.eval;
		}
		//Check if it is needed to increase or decrease the swarm size
		else {
			//THERE IS AN IMPROVEMENT
			if (prevGbest > global_best.eval){
				prevGbest = global_best.eval;
				iterWithoutImprvCounter = 0;
				iterWithImprvCounter++;
				if (iterWithImprvCounter == config->getPopTViterations()){
					//Try to remove one particle from the swarm
					if ((long)swarm.size()-1 > config->getInitialPopSize()){
						removeOneParticle(config);
						RNG::initializePermutation(config->getSwarmSize());
						updateTopologyConnections(config, -1, iteration);
						if (config->verboseMode())
							cout << "\tnotice::one particle removed from the swarm after " << config->getPopTViterations()
							<< " consecutive iterations where global_best has improved" << endl;
						partRemoved++;
					}
					else {
						if (config->verboseMode())
							cout << "\tnotice::there's been " << config->getPopTViterations()
							<< " without improvement, but swarm is already in its lower bound" << endl;
					}
				}
			}
			//THERE IS NOT IMPROMEVENT
			else{
				iterWithImprvCounter=0;
				iterWithoutImprvCounter++;
				if (iterWithoutImprvCounter == config->getPopTViterations()){
					//Try to add one particle to the swarm
					if ((long)swarm.size()+1 < config->getFinalPopSize()){
						//If there is space in the swarm: add one particle
						addParticles(problem, config, 1, iteration);
						RNG::initializePermutation(config->getSwarmSize());
						updateTopologyConnections(config, previous_size, iteration);
						if (config->verboseMode())
							cout << "\tnotice::one particle added to the swarm after " << config->getPopTViterations()
							<< " consecutive iterations without a new global_best" << endl;
						partAdded++;
					}
					else {
						//Reinitialize the worst particle
						int worstP_pos = 0;
						double worstP_eval = swarm.at(0)->getPbestEvaluation();
						//Find the position in the swarm of the worst particle
						for(unsigned int i=1; i<swarm.size(); i++){
							if (swarm.at(i)->getPbestEvaluation() >= worstP_eval){
								worstP_pos = i;
								worstP_eval = swarm.at(i)->getPbestEvaluation();
							}
						}
						swarm.at(worstP_pos)->reInitPosUniform(config);
						if (config->verboseMode()) {
							cout << "\t\tnotice::Particle [" << swarm.at(worstP_pos)->getID() << "] was reinitialized to a random position"; //<< endl; //remove
						}
					}
				}
			}
		}
	}
	if (config->getPopulationCS() == POP_INCREMENTAL){
		particleToAdd = config->getParticlesToAdd();
		//Add k particle per iteration
		if (previous_size+particleToAdd <= config->getFinalPopSize()){
			addParticles(problem, config, particleToAdd, iteration);
			RNG::initializePermutation(config->getSwarmSize());
			updateTopologyConnections(config, previous_size, iteration);
		}
		else{
			//See if we can add the difference
			particleToAdd = config->getFinalPopSize()-previous_size;
			if ( particleToAdd > 0){
				addParticles(problem, config, particleToAdd, iteration);
				RNG::initializePermutation(config->getSwarmSize());
				updateTopologyConnections( config, previous_size, iteration);
			}
		}
	}
	if (config->verboseMode()){
		cout << "\tnotice::particles added so far: " << partAdded << endl;
		cout << "\tnotice::particles removed so far: " << partRemoved << endl;
	}
}

void Swarm::updateTopologyConnections(Configuration* config, long previous_size, long int iteration){
	int a,b,c;
	if (previous_size > 0) { //PARTICLES WERE ADDED -- EXTEND THE TOPOLOGY
		switch (config->getTopology()){
		case TOP_FULLYCONNECTED:
			//Clear connections
			for(unsigned int i=0; i<swarm.size(); i++){
				swarm.at(i)->neighbours.clear();
			}
			//Create new connections
			createFullyConnectedTopology();
			break;
		case TOP_HIERARCHICAL:
			for(unsigned int i=previous_size; i<swarm.size(); i++){
				for(unsigned int j=0; j<swarm.size(); j++){
					swarm.at(i)->addNeighbour(swarm.at(j));
					if (j<previous_size)
						swarm.at(j)->addNeighbour(swarm.at(i));
				}
			}
			updateHierarchical(config->getBranchingDegree(),previous_size);
			break;
		case TOP_RING:
			if (swarm.size() > 3){
				//Remove previous connect between the first and the last particle
				swarm.at(0)->eraseNeighborbyID(previous_size-1);
				swarm.at(previous_size-1)->eraseNeighborbyID(0);
				for(unsigned int i=previous_size; i<swarm.size(); i++){
					a=i-1;
					b=i+1;
					if(i==0)
						a=swarm.size()-1;
					if(i==(swarm.size()-1)){
						b=0;
						//Reconnect first particle with the last particle
						swarm.at(0)->addNeighbour(swarm.at(i));
						swarm.at(previous_size-1)->addNeighbour(swarm.at(previous_size));
					}
					swarm.at(i)->addNeighbour(swarm.at(a));
					swarm.at(i)->addNeighbour(swarm.at(b));
				}
			}
			else {
				//Reconnect the whole swarm
				for(unsigned int i=0; i<swarm.size(); i++)
					swarm.at(i)->neighbours.clear();
				createRingTopology();
			}
			break;
		case TOP_WHEEL:
			for(unsigned int i=previous_size; i<swarm.size(); i++){
				swarm.at(i)->addNeighbour(swarm.at(0));
				swarm.at(0)->addNeighbour(swarm.at(i));
			}
			break;
		case TOP_RANDOM:{
			long int randomEdge;
			for(unsigned int i=previous_size; i<swarm.size(); i++){
				randomEdge = (int)floor(RNG::randVal(0.0,(double)swarm.size()));
				//randomEdge = RNG::getPermutationElement(i);
				if (randomEdge == i){
					randomEdge = (int)floor(RNG::randVal(0.0,(double)swarm.size()));
					swarm.at(i)->addNeighbour(swarm.at(randomEdge));
				}
				else
					swarm.at(i)->addNeighbour(swarm.at(randomEdge));
			}
		}
		break;
		case TOP_TIMEVARYING:
			if (iteration < config->getTopologySchedule()){
				//We connect a particle randomly with swarm ensuring that the particle is connected to the adjacent neighbors (RING)
				//Get average connections number of the swarm.
				int averageConnections = 0;
				for(unsigned int i=0; i<previous_size; i++){
					averageConnections += swarm.at(i)->neighbours.size();
				}
				averageConnections = (int)floor((double)averageConnections/previous_size);
				//			cout << "\tvar::averageConnections: " << averageConnections << endl;
				//			cout << "\tvar::previous_size: " << previous_size << endl;

				//swarm.at(0)->eraseNeighborbyID(previous_size-1);
				//swarm.at(previous_size-1)->eraseNeighborbyID(0);
				unsigned int a,b;
				for(unsigned int i=previous_size; i<swarm.size(); i++){
					//First connect the new particles in RING
					a=i-1;
					b=i+1;
					if(i==0)
						a=swarm.size()-1;
					if(i==(swarm.size()-1)){
						b=0;
						//Reconnect first particle with the last particle
						swarm.at(0)->addNeighbour(swarm.at(i));
						//Reconnect previous last particle with the next one
						swarm.at(previous_size-1)->addNeighbour(swarm.at(previous_size));
					}
					swarm.at(i)->addNeighbour(swarm.at(i));
					swarm.at(i)->addNeighbour(swarm.at(a));
					swarm.at(i)->addNeighbour(swarm.at(b));

					//After connect the new particles with #averageConnections random neighbors
					for(int j=0; j<averageConnections-3; j++){
						//unsigned int randomEdge = RNG::getPermutationElement(j);
						unsigned int randomEdge = (unsigned int)floor(RNG::randVal(0.0,(double)swarm.size()));
						if (randomEdge != a && randomEdge != b && randomEdge != i){
							swarm.at(i)->addNeighbour(swarm.at(randomEdge));
							swarm.at(randomEdge)->addNeighbour(swarm.at(i));
						}
					}
				}
			}
			else { //RING -- After the topology update period is finished
				//Remove previous connect between the first and the last particle
				swarm.at(0)->eraseNeighborbyID(previous_size-1);
				swarm.at(previous_size-1)->eraseNeighborbyID(0);
				for(unsigned int i=previous_size; i<swarm.size(); i++){
					a=i-1;
					b=i+1;
					if(i==0)
						a=swarm.size()-1;
					if(i==(swarm.size()-1)){
						b=0;
						//Reconnect first particle with the last particle
						swarm.at(0)->addNeighbour(swarm.at(i));
						swarm.at(previous_size-1)->addNeighbour(swarm.at(previous_size));
					}
					//swarm.at(i)->addNeighbour(swarm.at(i));
					swarm.at(i)->addNeighbour(swarm.at(a));
					swarm.at(i)->addNeighbour(swarm.at(b));
				}
			}
			break;
		case TOP_VONNEUMANN:
			//If the swarm is bigger than 5 we can simply reconnect the first two particle according to the new size.
			//The last one will be reconnected using the second for loop
			//cout << "\n swarm.size(): "  << swarm.size() << endl;
			if (swarm.size() >= 5){
				//Reconnect two first and the last particle
				for(unsigned int i=0; i<2; i++){
					swarm.at(i)->neighbours.clear();
					c=i+1;
					if(i==0){
						a=swarm.size()-1;
						b=a-1;
					}
					if (i==1){
						a=swarm.size()-1;
						b=0;
					}

					swarm.at(i)->addNeighbour(swarm.at(a));
					swarm.at(i)->addNeighbour(swarm.at(b));
					swarm.at(i)->addNeighbour(swarm.at(c));
					//cout << "Reconnect first two particles" << endl; //remove
				}
				//Connect the new particles added and the former last one in the previous iteration
				swarm.at(previous_size)->neighbours.clear();
				for(unsigned int i=previous_size; i<swarm.size(); i++){
					a=i-1;
					b=a-1;
					c=i+1;
					if(i==0){
						a=swarm.size()-1;
						b=a-1;
					}
					if (i==1){
						a=swarm.size()-1;
						b=0;
					}
					if(i==(swarm.size()-1))
						c=0;

					swarm.at(i)->addNeighbour(swarm.at(a));
					swarm.at(i)->addNeighbour(swarm.at(b));
					swarm.at(i)->addNeighbour(swarm.at(c));
					//cout << "Reconnect last particle" << endl; //remove
				}
			}
			else {
				//Reconnect the whole swarm
				for(unsigned int i=0; i<swarm.size(); i++)
					swarm.at(i)->neighbours.clear();
				createVonNeumannTopology();
			}
			break;
		}
	}
	//PARTICLES WERE REMOVED -- RECONNECT THE TOPOLOGY
	else {
		//Reconnect the swarm
		switch (config->getTopology()) {
		case TOP_FULLYCONNECTED:
			//Clear old connections
			for(unsigned int i=0; i<swarm.size(); i++){
				swarm.at(i)->neighbours.clear();
			}
			//Create new fully connected topology
			createFullyConnectedTopology();
			break;
		case TOP_RING:
			//Clear old connections
			for(unsigned int i=0; i<swarm.size(); i++){
				swarm.at(i)->neighbours.clear();
			}
			//Create new ring topology
			createRingTopology();
			break;
		case TOP_WHEEL:
			//Clear old connections
			for(unsigned int i=0; i<swarm.size(); i++){
				swarm.at(i)->neighbours.clear();
			}
			//Create new wheel topology
			createWheelTopology();
			break;
		case TOP_RANDOM:
			//Clear old connections
			for(unsigned int i=0; i<swarm.size(); i++){
				swarm.at(i)->neighbours.clear();
			}
			//Create new random edge topology
			createRandomEdge();
			break;
		case TOP_VONNEUMANN:
			//Clear old connections
			for(unsigned int i=0; i<swarm.size(); i++){
				swarm.at(i)->neighbours.clear();
			}
			//Create new Von Neumann
			createVonNeumannTopology();
			break;
		case TOP_TIMEVARYING:
			if (iteration < config->getTopologySchedule()){
				//We connect a particle randomly with swarm ensuring that the particle is connected to the adjacent neighbors (RING)
				//Get average connections number of the swarm.
				int averageConnections = 0;
				for(unsigned int i=0; i<swarm.size(); i++){
					averageConnections += swarm.at(i)->neighbours.size();
				}
				averageConnections = (int)floor((double)averageConnections/swarm.size()-1);
				//cout << "\tvar::averageConnections: " << averageConnections << endl;
				//cout << "\tvar::previous_size: " << previous_size << endl;

				//Clear old connections
				for(unsigned int i=0; i<swarm.size(); i++){
					swarm.at(i)->neighbours.clear();
				}

				//swarm.at(0)->eraseNeighborbyID(previous_size-1);
				//swarm.at(previous_size-1)->eraseNeighborbyID(0);
				unsigned int a,b;
				for(unsigned int i=0; i<swarm.size(); i++){
					//First connect the new particles in RING
					a=i-1;
					b=i+1;
					if(i==0)
						a=swarm.size()-1;
					if(i==(swarm.size()-1))
						b=0;

					swarm.at(i)->addNeighbour(swarm.at(i));
					swarm.at(i)->addNeighbour(swarm.at(a));
					swarm.at(i)->addNeighbour(swarm.at(b));

					bool noAdd = false;
					//After connect the new particles with #averageConnections random neighbors
					for(int j=0; j<averageConnections-3; j++){
						//unsigned int randomEdge = RNG::getPermutationElement(j);
						unsigned int randomEdge = (unsigned int)floor(RNG::randVal(0.0,(double)swarm.size()));
						if (randomEdge != a && randomEdge != b && randomEdge !=i){
							for(unsigned int j=0; j<swarm.at(i)->neighbours.size(); j++){
								if (swarm.at(randomEdge)->getID() == swarm.at(i)->neighbours.at(j)->getID())
									noAdd = true;
							}
							if (noAdd == false){
								swarm.at(i)->addNeighbour(swarm.at(randomEdge));
								swarm.at(randomEdge)->addNeighbour(swarm.at(i));
							}
						}
					}
				}
			}
			else { //RING -- After the topology update period is finished
				//Clear old connections
				for(unsigned int i=0; i<swarm.size(); i++){
					swarm.at(i)->neighbours.clear();
				}
				createRingTopology();
			}
			break;
		}
	}
}

void Swarm::removeOneParticle(Configuration* config){
	//Add particles to the swarm
	int pID = swarm.at(0)->getID();
	double pQuality = swarm.at(0)->getPbestEvaluation();

	//Find worst particle in the last level if TOP_HIERARCHICAL is being used
	if (config->getTopology() == TOP_HIERARCHICAL){
		int childsInLastLevel = 0;
		//Get the number of particles in the last level complete
		for (int i=0; i<pow(config->getBranchingDegree(),lastLevelComplete+1); i++){
			if (hierarchy.at(lastLevelComplete+1).at(i) != -2) {
				//Since hierarchy contains the ID of particles, we need to look for the particle in swarm with that ID
				for (unsigned int j=0;j<swarm.size();j++){
					if (swarm.at(j)->getID() == hierarchy.at(lastLevelComplete+1).at(i)){
						//If its the first iteration there's nothing to compare, just keep the quality in pQuality
						if (i>0){
							pQuality = swarm.at(j)->getPbestEvaluation();
							pID = hierarchy.at(lastLevelComplete+1).at(i);
						}
						else {
							if (swarm.at(j)->getPbestEvaluation() >= pQuality){
								pID = hierarchy.at(lastLevelComplete+1).at(i);
								pQuality = swarm.at(j)->getPbestEvaluation();
							}
						}
					}
				}
				childsInLastLevel++;
			}
		}
		//Remove particle ID from the hierarchy
		for (int i=0; i<pow(config->getBranchingDegree(),lastLevelComplete+1); i++){
			if (hierarchy.at(lastLevelComplete+1).at(i) == pID) {
				hierarchy.at(lastLevelComplete+1).at(i) = -2;
				break;
			}
		}
		//Update lastLevelComplete in case there are no more particles in that level
		if(childsInLastLevel == 1)
			lastLevelComplete--;
	}
	//Find worst particle
	else {
		for (unsigned int i=1;i<swarm.size();i++){
			if (swarm.at(i)->getPbestEvaluation() >=  pQuality){
				pID = swarm.at(i)->getID();
				pQuality = swarm.at(i)->getPbestEvaluation();
			}
		}
	}
	//Remove particle from the neighbors set of other particles
	for (unsigned int i=0;i<swarm.size();i++){
		swarm.at(i)->eraseNeighborbyID(pID);
	}
	if (config->verboseMode())
		cout << "\tnotice::p[" << pID << "] erased from the neighborhood set of all other particles" << endl;
	if (config->verboseMode())
		cout << "\tnotice::previous swarm.size() = " << swarm.size() << endl;
	//Remove particle from the swarm
	for(unsigned int i=0;i<swarm.size();i++){
		if(pID == swarm.at(i)->getID()){
			swarm.erase(swarm.begin()+i);
			if (config->verboseMode())
				cout << "\tnotice::p[" << pID << "] removed from the swarm" << endl;
			break;
		}
	}
	if (config->verboseMode())
		cout << "\tnotice::new swarm.size() = " << swarm.size() << endl;
	//Update variable size
	config->setSwarmSize(swarm.size()); //long int particles
}


void Swarm::addParticles(Problem* problem, Configuration* config, int numOfParticles, long int iteration){
	if ((long)swarm.size() <= config->getFinalPopSize()){
		//Add particles to the swarm
		int current_size = swarm.size();
		long int partID = swarm.at(0)->getID();

		//Find the particle with the largest ID number
		for(unsigned int j=1;j<swarm.size();j++){
			if (swarm.at(j)->getID() > partID)
				partID = swarm.at(j)->getID();
		}

		for (long int i=current_size; i<current_size+numOfParticles; i++){
			partID++;
			Particle* aParticle = new Particle(problem, config, partID, iteration);
			swarm.push_back(aParticle);
			if (swarm.at(i)->getPbestEvaluation() < global_best.eval){
				updateGlobalBest(swarm.at(i)->getPbestPosition(), swarm.at(i)->getPbestEvaluation());
				best_particle = swarm.at(i);
			}
		}
		//Update variable size
		config->setSwarmSize(swarm.size()); //long int particles
	}
}

void Swarm::reinitializeParticlePosition(Configuration* config){
	//Reinitialize particles that are too close to gbest
	if (config->useReinitialization()){
		for (unsigned int i=0;i<swarm.size();i++){
			//Check that the particle is different to itself before reinitializing its position
			if (swarm.at(i)->getID() != swarm.at(i)->getgBestID()){
				double* gBestPos = 0;
				double* particlePos = swarm.at(i)->getCurrentPosition();
				double similarity = 0; //distance between x and gBest
				//Find gBest of particle by id
				for (unsigned int r=0;r<swarm.size();r++){
					if (swarm.at(i)->getgBestID() == swarm.at(r)->getID()){
						gBestPos = swarm.at(r)->getCurrentPosition();
					}
				}
				//Compute similarity between particle and gBest
				for (unsigned int j=0;j<config->getProblemDimension();j++){
					similarity +=  particlePos[j] / gBestPos[j];
				}
				similarity = similarity/config->getProblemDimension();
				if (fabs(1-similarity) < REINIT_PRECISION){
					if (config->verboseMode()){
						cout << "\tparticle:: [" << i << "] pos: [ ";
						for(unsigned int h=0;h<config->getProblemDimension();h++){
							cout << gBestPos[h] << " ";
						}
						cout << "]" << endl;
						cout << "\tgBest::pos: [";
						for(unsigned int k=0;k<config->getProblemDimension();k++){
							cout << particlePos[k] << " ";
						}
						cout << "]" << endl;
					}
					//Reinitialize particle to a random position and set v=0
					swarm.at(i)->reInitPosUniform(config);
					if (config->verboseMode()) {
						cout << "\t\tnotice::Particle [" << i << "] was reinitialized to a random position"; //<< endl; //remove
					}
				}
			}
		}
	}
}

/*Update global best solution found */
void Swarm::updateGlobalBest(double* new_x, double eval){
	for (int i=0;i<problem->getProblemDimension();i++){
		global_best.x[i]=new_x[i];
	}
	global_best.eval=eval;
}

void Swarm::printGbest(unsigned int dimensions){
	//print best solution
	cout << "[ " ;
	for(unsigned int i=0; i< dimensions; i++){
		cout << scientific << getGlobalBest().x[i] << "  ";
	}
	cout << " ]\n" << endl;
}


Solution Swarm::getGlobalBest(){
	return global_best;
}

void Swarm::decomposePhi2(int modelOfInflu, int part_id, int numInformants){
	if (modelOfInflu == MOI_FI)
		swarm.at(part_id)->setPhi2(swarm.at(part_id)->getPhi2()/numInformants);
}

void Swarm::computeAccelerationCoefficients(Configuration* config, long int iteration){
	//If the strategy involves all particles using the same acceleration coefficients value,
	//it is more efficient to compute it once at the beginning of the iteration
	//Constant value
	switch (config->getAccelCoeffCS()) {
	case AC_CONSTANT:{
		for (unsigned int i=0; i<swarm.size(); i++){
			swarm.at(i)->setPhi1(config->getPhi1());
			swarm.at(i)->setPhi2(config->getPhi2());
		}
	} break;
	//Random values within bounds
	case AC_RANDOM:{
		for (unsigned int i=0; i<swarm.size(); i++){
			swarm.at(i)->setPhi1(problem->getRandomX(config->getInitialPhi1(),config->getFinalPhi1()));
			swarm.at(i)->setPhi2(problem->getRandomX(config->getInitialPhi2(),config->getFinalPhi2()));
		}
	} break;
	case AC_EXTRAPOLATED:{
		for (unsigned int i=0; i<swarm.size(); i++){
			double varPhi1 = exp((double)(-iteration/config->getMaxIterations()));
			double distanceToGbest = (swarm.at(swarm.at(i)->getgBestID())->getCurrentEvaluation()-swarm.at(i)->getCurrentEvaluation())/
					swarm.at(swarm.at(i)->getgBestID())->getCurrentEvaluation();
			swarm.at(i)->setPhi1(varPhi1);
			swarm.at(i)->setPhi2(exp(varPhi1*distanceToGbest));
		}
	} break;
	case AC_TIME_VARYING:{
		double varPhi1 = (config->getInitialPhi1()-config->getFinalPhi1()) *
				(double)(iteration/config->getMaxIterations()) + config->getInitialPhi1();
		double varPhi2 = (config->getInitialPhi2()-config->getFinalPhi2()) *
				(double)(iteration/config->getMaxIterations()) + config->getInitialPhi2();
		for (unsigned int i=0; i<swarm.size(); i++){
			swarm.at(i)->setPhi1( varPhi1 );
			swarm.at(i)->setPhi2( varPhi2 );
		}
	} break;
	}
}

void Swarm::updatePerturbationVariables(Configuration* config, double previousGbest_eval, double currentGbest_eval, long int iteration){

	if (config->getMagnitude1CS() == MAGNITUDE_SUCCESS){
		//Success
		if (previousGbest_eval != currentGbest_eval){
			config->set_mag1_sc(config->get_mag1_sc()+1); //increase success
			config->set_mag1_fc(0); //set failures to 0
		}
		//Failure
		else{
			config->set_mag1_fc(config->get_mag1_fc()+1); //increase failures
			config->set_mag1_sc(0); //set successes to 0
		}

		//Update magnitude
		if (config->get_mag1_sc() > config->getMag1_parm_success()){
			config->setMagnitude1(config->getMagnitude1() * 2.0);
		}
		if (config->get_mag1_fc() > config->getMag1_parm_failure()){
			config->setMagnitude1(config->getMagnitude1() * 0.5);
		}
	}

	if (config->getMagnitude2CS() == MAGNITUDE_SUCCESS){
		//Success
		if (previousGbest_eval != currentGbest_eval){
			config->set_mag2_sc(config->get_mag2_sc()+1); //increase success
			config->set_mag2_fc(0); //set failures to 0
		}
		//Failure
		else{
			config->set_mag2_fc(config->get_mag2_fc()+1); //increase failures
			config->set_mag2_sc(0); //set successes to 0
		}

		//Update value of alpha
		//Increase magnitude
		if (config->get_mag2_sc() > config->getMag2_parm_success()){
			if (config->getPerturbation2CS() == PERT2_RECTANGULAR || config->getPerturbation2CS() == PERT2_NOISY)
				config->setMagnitude2(config->getMagnitude2()*2.0);
		}
		//Decrease magnitude
		if (config->get_mag2_fc() > config->getMag2_parm_failure()){
			if (config->getPerturbation2CS() == PERT2_RECTANGULAR || config->getPerturbation2CS() == PERT2_NOISY)
				config->setMagnitude2(config->getMagnitude2()*0.5);
		}
	}

	//	if (config->getMagnitude2CS() == MAGNITUDE_CONSTANT){
	//		config->setPert2_alpha(config->getMagnitude2());
	//		config->setPert2_delta(config->getMagnitude2());
	//	}

}

/* Topologies */
void Swarm::createFullyConnectedTopology(){       //All particles are neighbor among them
	for(unsigned int i=0;i<swarm.size();i++){
		for(unsigned int j=0;j<swarm.size();j++){
			swarm.at(i)->addNeighbour(swarm.at(j));
		}
	}
}

void Swarm::createRingTopology(){       //Every particle is neighbor of the adjacent particles
	if (swarm.size() > 2){
		int a,b;
		for(unsigned int i=0;i<swarm.size();i++){
			a=i-1;
			b=i+1;
			if(i==0)
				a=swarm.size()-1;
			if(i==(swarm.size()-1))
				b=0;

			swarm.at(i)->addNeighbour(swarm.at(i));
			swarm.at(i)->addNeighbour(swarm.at(a));
			swarm.at(i)->addNeighbour(swarm.at(b));
		}
	}
	else {
		for(unsigned int i=0;i<swarm.size();i++){
			for(unsigned int j=0;j<swarm.size();j++){
				if (i != j)
					swarm.at(i)->addNeighbour(swarm.at(j));
			}
		}
	}
}

void Swarm::createWheelTopology(){        //Particles are neighbors of one central particle
	for(unsigned int i=1;i<swarm.size();i++){
		swarm.at(i)->addNeighbour(swarm.at(0));
		swarm.at(0)->addNeighbour(swarm.at(i));
	}
}

void Swarm::createRandomEdge(){			//Or random edge topology
	long int randomEdge;
	for(unsigned int i=0;i<swarm.size();i++){
		randomEdge = (int)floor(RNG::randVal(0.0,(double)swarm.size()));
		//randomEdge = RNG::getPermutationElement(i);
		if (randomEdge == i){
			randomEdge = (int)floor(RNG::randVal(0.0,(double)swarm.size()));
			swarm.at(i)->addNeighbour(swarm.at(randomEdge));
		}
		else
			swarm.at(i)->addNeighbour(swarm.at(randomEdge));
	}
}

void Swarm::createVonNeumannTopology(){
	if (swarm.size() > 3){
		int a,b,c;
		for(unsigned int i=0;i<swarm.size();i++){
			a=i-1;
			b=a-1;
			c=i+1;
			if(i==0){
				a=swarm.size()-1;
				b=a-1;
			}
			if (i==1){
				a=swarm.size()-1;
				b=0;
			}
			if(i==(swarm.size()-1))
				c=0;

			swarm.at(i)->addNeighbour(swarm.at(a));
			swarm.at(i)->addNeighbour(swarm.at(b));
			swarm.at(i)->addNeighbour(swarm.at(c));
		}
	}
	else {
		for(unsigned int i=0;i<swarm.size();i++){
			for(unsigned int j=0;j<swarm.size();j++){
				if (i != j)
					swarm.at(i)->addNeighbour(swarm.at(j));
			}
		}
	}
}

void Swarm::updateTimeVaryingTopology(Configuration* config, long int iterations){
	long int swarm_size;
	(config->getPopulationCS() == POP_CONSTANT) ? swarm_size = swarm.size() : swarm_size = config->getFinalPopSize();
	//	cout << "\t\tvar::topologyUpdatePeriod:  " <<config->getTopologyUpdatePeriod() << endl;
	//	cout << "\t\tvar::tschedule: " << config->getTopologySchedule() << endl;


	//Topology update: Following the progression n-2, n-3, ..., 2. (see esteps variable)
	if ((iterations > 0) &&	(iterations%config->getTopologyUpdatePeriod() == 0) &&
			((config->getEsteps() < swarm_size-3 && (config->getPopulationCS() == POP_CONSTANT))
					|| ((config->getEsteps() > 0 && swarm.size() > 4 ) && config->getPopulationCS() != POP_CONSTANT))){

		unsigned int removals = 0;
		RNG::shufflePermutation();
		unsigned int target = swarm_size-(2+config->getEsteps());

		double averageConnections = 0;
		for(unsigned int i=0; i<swarm.size(); i++){
			averageConnections += swarm.at(i)->neighbours.size();
		}
		averageConnections = averageConnections/swarm.size();
		//		cout << "\t\tvar::averageConnections: " << averageConnections << endl;
		//		cout << "\t\tvar::esteps: " << config->getEsteps() << endl;
		//		cout << "\t\tvar::iteration: " << iterations << endl;
		//		cout << "\t\tvar::target: " << swarm_size-(2+config->getEsteps()) << endl;
		//		cout << "\t\tvar::swarm_size: " << swarm_size << endl;
		while(removals < target && averageConnections > 3){
			try {// vector::at throws an out-of-range
				for(unsigned int i=0;i<swarm.size();i++){
					int particleIndex = RNG::getPermutationElement(i);
					int neighborID = -1;
					bool idFound = false;
					if (swarm.at(particleIndex)->neighbours.size() > 3 ){ //3 because a particle is a neighbor to itself
						//if (config->getPopulationCS() != POP_TIME_VARYING)
						if (config->getPopulationCS() == POP_CONSTANT || config->getPopulationCS() == POP_INCREMENTAL)
							neighborID = swarm.at(particleIndex)->getRandomNonAdjacentNeighborID(config);
						else {
							//Look for a neighbor based on the position of the particles in the swarm
							if (particleIndex == 0){ //Particle in the FIRST position of swarm
								for(unsigned int k=2; k<swarm.size()-1; k++){
									for (unsigned int j=0; j<swarm.at(particleIndex)->neighbours.size(); j++){
										if (swarm.at(k)->getID() == swarm.at(particleIndex)->neighbours.at(j)->getID()){
											neighborID = swarm.at(k)->getID();
											idFound = true;
											break;
										}
									}
									if (idFound == true) break;
								}
							}
							else if ((long unsigned) particleIndex == swarm.size()-1){//Particle in the LAST position of swarm
								for(unsigned int k=1; k<swarm.size()-2; k++){
									for (unsigned int j=0; j<swarm.at(particleIndex)->neighbours.size(); j++){
										if (swarm.at(k)->getID() == swarm.at(particleIndex)->neighbours.at(j)->getID()){
											neighborID = swarm.at(k)->getID();
											idFound = true;
											break;
										}
									}
									if (idFound == true) break;
								}
							}
							else{
								for(unsigned int k=0; k<swarm.size(); k++){
									for (unsigned int j=0; j<swarm.at(particleIndex)->neighbours.size(); j++){
										if (swarm.at(k)->getID() == swarm.at(particleIndex)->neighbours.at(j)->getID()){
											//Check left-side
											for (unsigned int l=0; l<swarm.at(particleIndex)->neighbours.size(); l++){
												if (swarm.at(k-1)->getID() != swarm.at(particleIndex)->neighbours.at(l)->getID()){
													//Check right-side
													for (unsigned int r=0; r<swarm.at(particleIndex)->neighbours.size(); r++){
														if (swarm.at(k+1)->getID() != swarm.at(particleIndex)->neighbours.at(r)->getID()){
															neighborID = swarm.at(k)->getID();
															idFound = true;
															break;
														}
													}
												}
												if (idFound == true) break;
											}
										}
										if (idFound == true) break;
									}
									if (idFound == true) break;
								}
							}
						}
						//Prevent an infinite loop
						if (neighborID == -1){
							removals++;
							break;
						}
						swarm.at(particleIndex)->eraseNeighborbyID(neighborID);
						swarm.at(neighborID)->eraseNeighborbyID(particleIndex);
						removals++;
					}
					if (removals == target)
						break;
				}
				//			cout << "\t\tvar::removals: " << removals << endl;
			}
			catch (const std::out_of_range& oor) {
				//std::cerr << "Out of Range error: " << oor.what() << '\n';
				break;
			}
		}
		//cout << "\t\tvar::removals: " << removals << endl;
		(config->getPopulationCS() == POP_CONSTANT || config->getPopulationCS() == POP_TIME_VARYING) ? config->setEsteps(config->getEsteps()+1) : config->setEsteps(config->getEsteps()-1);
		//		cout << "\t\tfunc::updateTimeVaryingTopology().status() -> correct" << endl;
	}
}

void Swarm::createHierarchical(int branching, int finalPopSize){
	//cout  << "\nCreating hierarchical topology..." << endl;
	//The topology is fully-connected, but we use a hierarchy as a model of influence
	long int firstPart = 1;		//id of the last particle that can be added in a complete level without exceeding size
	int actualSpace=1;			//actual number of nodes than can be stored in a given level. This depends on the swarm size and the branching degree
	int maxWidth = 0;
	int temp_LastLevelComplete = 0;
	long int temp_firstPart = 1;

	//Find last level that can be filled in the tree with max width and the id of the last particle
	while (size - pow(branching,lastLevelComplete) >= pow(branching,lastLevelComplete+1)) {
		lastLevelComplete++;
		firstPart+=pow(branching,lastLevelComplete);
	}
	//It can be the case that for a very large branching degree the firstPart variable
	//is larger than the size of the swarm. In those cases, we find the last level complete
	//without exceeding the swarm size
	while (firstPart > size){
		lastLevelComplete--;
		firstPart-=pow(branching,lastLevelComplete);
	}
	//Compute actual space
	for (int i=1; i<=lastLevelComplete; ++i)
		actualSpace+=pow(branching,i);
	if (firstPart > actualSpace)
		firstPart = actualSpace;
	//Constant population size
	if (finalPopSize == size){
		maxWidth = pow(branching,lastLevelComplete+1);
		temp_LastLevelComplete = lastLevelComplete;
		temp_firstPart = firstPart;
	}
	//Dynamic population size
	else {
		//recompute everything to get the maxWidth, which is used to allocate memory
		while (finalPopSize - pow(branching,temp_LastLevelComplete) >= pow(branching,temp_LastLevelComplete+1)){
			temp_LastLevelComplete++;
			temp_firstPart+=pow(branching,temp_LastLevelComplete);
		}
		while (temp_firstPart > finalPopSize){
			temp_LastLevelComplete--;
			temp_firstPart-=pow(branching,temp_LastLevelComplete);
		}
		maxWidth = pow(branching,temp_LastLevelComplete+1);
	}
	//cout << "Last level complete: \t" << lastLevelComplete << endl;
	//cout << "Actual space: \t\t" << actualSpace << endl;
	//cout << "First part: \t\t" << firstPart << endl;
	//cout << "Max width: \t\t" << maxWidth << endl;
	//cout << "Branching degree: \t" << branching << endl << endl;

	//Create a fully connected topology first
	for(unsigned int i=0;i<swarm.size()	;i++){
		for(unsigned int j=0;j<swarm.size();j++){
			swarm.at(i)->addNeighbour(swarm.at(j));
		}
	}

	//Initialize tree structure
	//Note that even when the population is dynamic, hierarchy already has the right size to allocate all
	//the population size
	//temp_LastLevelComplete+1 indicates the levels (depth of the tree)
	//maxWidth indicates maximum number of nodes in a level (max width of the tree)
	//	hierarchy = new int*[(temp_LastLevelComplete+1)];	//these are the level (depth of the tree)
	//		for (int i=0; i<=temp_LastLevelComplete+1; i++)
	//			hierarchy[i] = new int[maxWidth];		//these are the nodes in a level (max width of the tree)
	hierarchy.resize(temp_LastLevelComplete+2, vector<int>(maxWidth));

	for (int i=0; i<=temp_LastLevelComplete+1; i++){
		for (int j=0; j<maxWidth; j++)
			hierarchy.at(i).at(j) = -2;				//-2 indicates that the position is not in use
	}

	//tree variables
	int d = 0;				//node's degree
	int nodesCounter = 0; 	//every time we reach the max branching degree, we move to the next node
	int h = 1;				//height of the tree
	int width = 0;			//width of the level

	//root node
	int parent = hierarchy.at(0).at(0) = swarm.at(0)->getID();
	swarm.at(0)->setParent(-1); //the parent of root is NULL
	//We first fill all the levels that will be complete, i.e., the levels with maximum width
	for(unsigned int i=1;i<firstPart;i++){
		if (width < pow(branching,h)) {
			hierarchy.at(h).at(width) = swarm.at(i)->getID();
			swarm.at(i)->setParent(parent);
			width++;	//tree's degree
			d++;		//node's degree
			if (d==branching) {
				nodesCounter++;
				parent = hierarchy.at(h-1).at(nodesCounter);
				d=0;
			}
			if (width == pow (branching,h)){
				h++;
				width=0;
				nodesCounter=0;
				parent = hierarchy.at(h-1).at(nodesCounter);
			}
		}
	}
	//reset variables of the tree for the rest of the particles
	width = 0;
	nodesCounter=0;
	int iterCount = 0;
	parent = hierarchy.at(lastLevelComplete).at(nodesCounter);
	//Now we add one d at a time to each node in the last level complete
	for(unsigned int i=firstPart;i<swarm.size();i++){
		hierarchy.at(lastLevelComplete+1).at(width+(nodesCounter*(branching-1))+iterCount) = swarm.at(i)->getID();
		swarm.at(i)->setParent(parent);
		width++;		//last level degree
		nodesCounter++; //we add one d to each node to have a fair distribution
		parent = hierarchy.at(lastLevelComplete).at(nodesCounter);
		if (width == pow (branching,lastLevelComplete)){
			width=0;
			nodesCounter=0;
			iterCount++;
			parent = hierarchy.at(lastLevelComplete).at(nodesCounter);
		}
	}
	//updateTree(branching);
	//printTree(branching, swarm.size());
	//printAllParentNodes();
}

void Swarm::updateHierarchical(int branching, long previous_size){
	int childsInLastLevel = 0;
	int nodesToAdd = swarm.size()-previous_size;

	//printTree(branching,previous_size);

	//Get the number of particles in the last level complete
	for (int i=0; i<pow(branching,lastLevelComplete+1); i++){
		if (hierarchy.at(lastLevelComplete+1).at(i) != -2) {
			childsInLastLevel++;
		}
	}
	//cout << "\n\tThere are " << childsInLastLevel << " particle(s) in lastLevelComplete-->" << lastLevelComplete << endl; //remove
	//If the new set of particles to be added fits in lastLevelComplete
	if (pow(branching,lastLevelComplete+1)-childsInLastLevel >= nodesToAdd){
		//cout << "\n\t" << nodesToAdd << " new particle(s) will be added in lastLevelComplete+1... " << endl; //remove
		addParticlesInLastLevel(previous_size, swarm.size(), branching);
		if (nodesToAdd+childsInLastLevel == pow(branching,lastLevelComplete+1)){
			lastLevelComplete++;
			//cout << "\n\tlastLevelComplete+1 is now full [" << nodesToAdd+childsInLastLevel << "] particles, a new level will be added to the tree" << endl; //remove
		}
	}
	//If the number of particles to add is larger that the space in lastLevelComplete
	else {
		//Add the first part of the particles that fit in the last level
		long firstPart = (pow(branching,lastLevelComplete+1)-childsInLastLevel);
		addParticlesInLastLevel(previous_size, previous_size+firstPart, branching);
		//cout << "\n\t" << firstPart << " new particle(s) were added in lastLevelComplete+1... " << endl; //remove
		lastLevelComplete++;

		//Compute the number of nodes that still need to be added
		nodesToAdd = nodesToAdd-firstPart;
		int newLastLevel = lastLevelComplete;

		//Find last level that can be filled in the tree with max width and the id of the last particle
		while (nodesToAdd - pow(branching,newLastLevel) >= pow(branching,newLastLevel+1)) {
			newLastLevel++;
		}
		//cout << "\n\t" << newLastLevel-lastLevelComplete << " lastLevelComplete will be created" << endl; //remove

		//first and last are the ids of the particles that are going to be added in the level
		long first = previous_size+firstPart, last = 0;
		if (nodesToAdd < pow(branching,lastLevelComplete+1))
			last = first+nodesToAdd;
		else
			last = first+pow(branching,lastLevelComplete+1);


		for (int j=lastLevelComplete; j<=newLastLevel; j++){
			addParticlesInLastLevel(first, last, branching);
			if (j+1 <=newLastLevel){
				lastLevelComplete++;
				nodesToAdd = nodesToAdd-(last-first);
				first = last;
				if (nodesToAdd < pow(branching,lastLevelComplete+1))
					last = first + nodesToAdd;
				else
					last = first+pow(branching,lastLevelComplete+1);
			}
		}
		lastLevelComplete = newLastLevel;
	}
	//updateTree(branching);
	//printTree(branching,swarm.size());
	//printAllParentNodes();
}

void Swarm::addParticlesInLastLevel(int first, int last, int branching){ //first = previous_size, last =
	//tree variables
	int width = 0;
	int nodesCounter=0;
	int iterCount = 0;
	int parent = hierarchy.at(lastLevelComplete).at(nodesCounter);
	bool particleAdded = false;
	//Now we add one node at a time to each node in the last level complete
	for (int i=first; i<last; ++i){	//particles to add
		for (int j=0; j<pow(branching,lastLevelComplete+1); j++){ //maxWidth of the level
			if (hierarchy.at(lastLevelComplete+1).at(width+(nodesCounter*(branching-1))+iterCount) == -2){ //look for free positions
				hierarchy.at(lastLevelComplete+1).at(width+(nodesCounter*(branching-1))+iterCount) = swarm.at(i)->getID();
				swarm.at(i)->setParent(parent);
				particleAdded = true;
			}
			width++;		//last level degree
			nodesCounter++; //we add one node to each parent node to have a fair distribution
			parent = hierarchy.at(lastLevelComplete).at(nodesCounter);
			if (width == pow(branching,lastLevelComplete)){
				width=0;
				nodesCounter=0;
				iterCount++;
				parent = hierarchy.at(lastLevelComplete).at(nodesCounter);
			}
			if (particleAdded){
				break;
				particleAdded = false;
			}
		}
	}
}

void Swarm::getParticleParentsIDs(int particleID, int *ParentsArray1D){
	//Initialize the array to -2
	for (int i=0; i<=lastLevelComplete; i++)
		ParentsArray1D[i] = -2;

	//Find particle index in the swarm by its ID
	long int index=0;
	for(unsigned int i=0; i<swarm.size(); i++){
		if (particleID == swarm.at(i)->getID()){
			index = i;
			break;
		}
	}
	//cout << "\nParticle " << swarm.at(index)->getID() << ": " ;

	//Get direct parentNode ID
	int aParent = swarm.at(index)->getParent();
	if (aParent==-1){
		//cout << "Root node";
		//ParentsArray1D[0] = swarm.at(index)->getRandomNeighbor();
		ParentsArray1D[0] = swarm.at(index)->getID();
		swarm.at(particleID)->setgBestID(swarm.at(index)->getID());
	}
	else{
		int pos = 0;
		while (aParent != -1){
			ParentsArray1D[pos] = aParent;
			//cout << ParentsArray1D[pos] << " ";
			pos++;
			aParent = swarm.at(aParent)->getParent();
		}
	}
	//cout << endl ;
	//return ParentsArray1D;
}

int Swarm::getParticleNumParents(int particleID){
	int partents_counter=0;

	//Find particle index in the swarm by its ID
	long int index=0;
	for(unsigned int i=0; i<swarm.size(); i++){
		if (particleID == swarm.at(i)->getID()){
			index = i;
			break;
		}
	}

	int aParent = swarm.at(index)->getParent();
	while (aParent != -1){
		partents_counter++;
		aParent = swarm.at(aParent)->getParent();
	}
	//cout << endl ;
	return partents_counter;
}


void Swarm::updateTree(int branching){
	//Traverse the tree and update the position of the particles
	//reset indexes to traverse the entire tree
	//cout << endl << "Traversing the tree: " << endl;
	int h = 1;
	int width = 0;
	int iterCount = 0;
	int childCounter = 1;
	int newParent = 0;
	int newH = 0;
	int newWidth = 0;
	int parentNode = swarm.at(hierarchy.at(0).at(0))->getParent();	//start at the root top-bottom
	int parentH = 0;
	int parentWidth = -1;
	for(unsigned int i=1;i<size;i++){
		if (h <= lastLevelComplete){
			if (width < pow(branching,h)){ //max level width
				if (parentNode!=swarm.at(hierarchy.at(h).at(width))->getParent()){
					parentNode=swarm.at(hierarchy.at(h).at(width))->getParent();
					//cout << "Parent: " << parentNode << endl;	//Parent coordinates
					parentH = h-1;
					parentWidth++;
				}
				//cout << "\t\t" << hierarchy[h][width] << "->" << swarm.at(hierarchy[h][width])->getParent() << endl;
				if (swarm.at(hierarchy.at(h).at(width))->getCurrentEvaluation() < swarm.at(parentNode)->getCurrentEvaluation()){
					newParent = swarm.at(hierarchy.at(h).at(width))->getID();	//newParent coordinates (this is the child that becomes parent)
					newH = h;
					newWidth = width;
					//we change places only when newParent is different from getParent() of the last particle
					//this means that at least one of the children will become parent.
					//The id of this particle is saved in newParent
					if (width > 0 && (width+1) % branching == 0 && (newParent != swarm.at(hierarchy.at(h).at(width))->getParent())){
						swapNodes(newParent, newH, newWidth, parentNode, parentH, parentWidth, branching, h, width, iterCount);
					}
				}
				width++;
				childCounter++;
			}
			if (width == pow(branching,h)){
				h++;
				width=0;
				parentWidth=-1;
				parentNode=-1;
				//cout << endl << "Level " << h << ": " << endl;
			}
		}
		else {
			parentNode = hierarchy.at(h-1).at(iterCount);
			parentH = h-1;
			parentWidth++;
			newParent = parentNode;
			//cout << "Parent: " << parentNode << endl;

			for(int j=iterCount*branching;j<(iterCount*branching)+branching;j++){
				try{
					if (swarm.at(hierarchy.at(h).at(j))->getParent() == parentNode) {
						//cout << "\t\t" << hierarchy[h][j] << "->" << swarm.at(hierarchy[h][j])->getParent() << endl;
						childCounter++;
					}
					//Check if there is a new parent
					if (swarm.at(hierarchy.at(h).at(j))->getCurrentEvaluation() < swarm.at(parentNode)->getCurrentEvaluation()){
						newParent = swarm.at(hierarchy.at(h).at(j))->getID();	//newParent coordinates (this is the  that becomes parent)
						newH = h;
						newWidth = j;
					}
					//Switch parent and child after comparing with all children
					if (j == (iterCount*branching)+branching-1 && (newParent != swarm.at(hierarchy.at(h).at(j))->getParent())){
						swapNodes(newParent, newH, newWidth, parentNode, parentH, parentWidth, branching, h, width, iterCount);
					}
				}
				catch (const std::exception& e) {
					break;
				}
			}
			iterCount++;
		}
		if (childCounter==size)
			break;
	}
}

void Swarm::swapNodes(int newParent, int newH, int newWidth, int parentNode, int parentH, int parentWidth, int branching, int h, int width, int iterCount){
	//cout << "\t\t New parent: " << newParent << " coordinates: "  << newH << " "<< newWidth << endl;
	//cout << "\t\t Replaces: " << parentNode << " coordinates: "  << parentH << " "<< parentWidth << endl;

	//Swap child and parent IDs
	swarm.at(hierarchy.at(newH).at(newWidth))->setParent(swarm.at(hierarchy.at(parentH).at(parentWidth))->getParent());
	swarm.at(hierarchy.at(parentH).at(parentWidth))->setParent(newParent); //parent

	//Swap places
	hierarchy.at(parentH).at(parentWidth) = newParent;
	hierarchy.at(newH).at(newWidth) = parentNode;

	//cout << "\t\t s" << hierarchy[newH][newWidth] << "->" << swarm.at(hierarchy[newH][newWidth])->getParent() << endl;
	//cout << "\t\t s" << hierarchy[parentH][parentWidth] << "->" << swarm.at(hierarchy[parentH][parentWidth])->getParent() << endl;

	if (h <= lastLevelComplete) {
		//Update brothers parentID
		for (int k=width-(branching-1); k<=width; k++){
			swarm.at(hierarchy.at(h).at(k))->setParent(newParent);
			//cout << "\t\t b" << hierarchy[h][k] << "->" << swarm.at(hierarchy[h][k])->getParent() << endl;

		}
		//Update any children that the old child (now parent) may have had
		for (int k=branching*width; k<(branching*width)+branching; k++){
			try {
				swarm.at(hierarchy.at(h+1).at(k))->setParent(parentNode);
				//cout << "\t\t c" << hierarchy[h+1][k] << "->" << swarm.at(hierarchy[h+1][k])->getParent() << endl;
			}
			catch (const std::exception& e) {
				break;
			}
		}
	}
	if (h == lastLevelComplete+1) {
		//Update brothers parentID
		for(int k=iterCount*branching;k<(iterCount*branching)+branching;k++){
			try{
				swarm.at(hierarchy.at(h).at(k))->setParent(newParent);
				//cout << "\t\t b" << hierarchy[h][k] << "->" << swarm.at(hierarchy[h][k])->getParent() << endl;
			}
			catch (const std::exception& e) {
				break;
			}
		}
	}
}

void Swarm::printAllParentNodes(){
	int parentNode = 0;
	for(unsigned int i=0; i<swarm.size(); i++){
		cout << "Particle " << i << ": " ;
		parentNode = swarm.at(i)->getParent();

		if (parentNode==-1){
			cout << "Root node";
		}
		else{
			while (parentNode != -1){
				cout << parentNode << " ";
				parentNode = swarm.at(parentNode)->getParent();
			}
		}
		cout << endl ;
	}
}

void Swarm::printTree(int branching, long swarm_size) {
	//Raw printing of the structure
	cout << endl << endl;
	for (int i=0; i<=lastLevelComplete+1; i++){
		cout << "Level " << i << ": ";
		for (int j=0; j<pow(branching,lastLevelComplete+1); j++)
			cout << hierarchy.at(i).at(j) << " ";
		cout << endl;
	}

	//Print tree -- this is the same algorithm we use to traverse the tree and update the position of the particles
	cout << "\n\nRoot node: " << endl;
	cout << "\t\t" << hierarchy.at(0).at(0) << "->" << swarm.at(hierarchy.at(0).at(0))->getParent() << endl << "Level 1: " << endl;
	//reset indexes to traverse the entire tree
	int h = 1;
	int width = 0;
	int iterCount = 0;
	int childCounter = 1;
	int parentNode = swarm.at(hierarchy.at(0).at(0))->getParent();
	for(unsigned int i=1;i<swarm_size;i++){
		if (h <= lastLevelComplete){
			if (width < pow(branching,h)){ //max level width
				if (parentNode!=swarm.at(hierarchy.at(h).at(width))->getParent()){
					parentNode=swarm.at(hierarchy.at(h).at(width))->getParent();
					cout << "Parent: " << parentNode << endl;
				}
				cout << "\t\t" << hierarchy.at(h).at(width) << "->" << swarm.at(hierarchy.at(h).at(width))->getParent() << endl;
				width++;
				childCounter++;
			}
			if (width == pow(branching,h)){
				h++;
				width=0;
				cout << "Level " << h << ": " << endl;
			}
		}
		else {
			parentNode = hierarchy.at(h-1).at(iterCount);
			cout << "Parent: " << parentNode << endl;
			for(int j=iterCount*branching;j<(iterCount*branching)+branching;j++){
				try{
					if (swarm.at(hierarchy.at(h).at(j))->getParent() == parentNode) {
						cout << "\t\t" << hierarchy.at(h).at(j) << "->" << swarm.at(hierarchy.at(h).at(j))->getParent() << endl;
						childCounter++;
					}
				}
				//This exception is thrown when hierarchy[][] = -2 and we try to access a position of swarm.at(-2), which does not exist
				catch (const std::exception& e) {
					//cout << "h[" << h << "] j[" << j << "] iter " << iterCount << endl;
					break;
				}
			}
			iterCount++;
		}
		if ((unsigned)childCounter==swarm.size())
			break;
	}
}


//void Swarm::updateTree(int branching){
//	//Traverse the tree and update the position of the particles
//	//reset indexes to traverse the entire tree
//	//cout << endl << "Traversing the tree: " << endl;
//	int h = 1;
//	int width = 0;
//	int iterCount = 0;
//	int childCounter = 1;
//	int newParent = 0;
//	int newH = 0;
//	int newWidth = 0;
//	//get Root node ID
//	int parentNode;
//	int currentPNodeID;
//	int currentPNodeIndex;
//	for (unsigned int i=0;i<swarm.size();i++){
//		if (swarm.at(i)->getID() == hierarchy.at(0).at(0)){
//			parentNode = swarm.at(i)->getParent();
//			break;
//		}
//	}
//	//int parentNode = swarm.at(hierarchy.at(0).at(0))->getParent();	//start at the root top-bottom
//	int parentH = 0;
//	int parentWidth = -1;
//	for(unsigned int i=1;i<swarm.size();i++){
//		if (h <= lastLevelComplete){
//			if (width < pow(branching,h)){ //max level width
//				//Get particle ID and position in swarm
//				for (unsigned int l=0;l<swarm.size();l++){
//					if (swarm.at(l)->getID() == hierarchy.at(h).at(width)){
//						currentPNodeID = swarm.at(l)->getParent();
//						currentPNodeIndex = l;
//						break;
//					}
//				}
//				//if (parentNode!=swarm.at(hierarchy.at(h).at(width))->getParent()){
//				//	parentNode=swarm.at(hierarchy.at(h).at(width))->getParent();
//				if (parentNode!=currentPNodeID){
//					parentNode=currentPNodeID;
//					//cout << "Parent: " << parentNode << endl;	//Parent coordinates
//					parentH = h-1;
//					parentWidth++;
//				}
//				//cout << "\t\t" << hierarchy[h][width] << "->" << swarm.at(hierarchy[h][width])->getParent() << endl;
//				//if (swarm.at(hierarchy.at(h).at(width))->getCurrentEvaluation() < swarm.at(parentNode)->getCurrentEvaluation()){
//				//	newParent = swarm.at(hierarchy.at(h).at(width))->getID();	//newParent coordinates (this is the child that becomes parent)
//				if (swarm.at(currentPNodeIndex)->getCurrentEvaluation() < swarm.at(parentNode)->getCurrentEvaluation()){
//					newParent = swarm.at(currentPNodeIndex)->getID();	//newParent coordinates (this is the child that becomes parent)
//					newH = h;
//					newWidth = width;
//					//we change places only when newParent is different from getParent() of the last particle
//					//this means that at least one of the children will become parent.
//					//The id of this particle is saved in newParent
//					//if (width > 0 && (width+1) % branching == 0 && (newParent != swarm.at(hierarchy.at(h).at(width))->getParent()))
//					//	swapNodes(newParent, newH, newWidth, parentNode, parentH, parentWidth, branching, h, width, iterCount);
//					if (width > 0 && (width+1) % branching == 0 && (newParent != swarm.at(currentPNodeIndex)->getParent()))
//						swapNodes(newParent, newH, newWidth, parentNode, parentH, parentWidth, branching, h, width, iterCount);
//				}
//				width++;
//				childCounter++;
//			}
//			if (width == pow(branching,h)){
//				h++;
//				width=0;
//				parentWidth=-1;
//				parentNode=-1;
//				//cout << endl << "Level " << h << ": " << endl;
//			}
//		}
//		else {
//			//Get particle ID and position in swarm
//			for (unsigned int k=0;k<swarm.size();k++){
//				if (swarm.at(k)->getID() == hierarchy.at(h-1).at(iterCount)){
//					parentNode = swarm.at(k)->getParent();
//					break;
//				}
//			}
//			// parentNode = hierarchy.at(h-1).at(iterCount);
//			parentH = h-1;
//			parentWidth++;
//			newParent = parentNode;
//			//cout << "Parent: " << parentNode << endl;
//
//			for(int j=iterCount*branching;j<(iterCount*branching)+branching;j++){
//				//Get particle ID and position in swarm
//				for (unsigned int l=0;l<swarm.size();l++){
//					if (swarm.at(l)->getID() == hierarchy.at(h).at(j)){
//						currentPNodeID = swarm.at(l)->getParent();
//						currentPNodeIndex = l;
//						break;
//					}
//				}
//				try{
//					//if (swarm.at(hierarchy.at(h).at(j))->getParent() == parentNode) {
//					if (currentPNodeID == parentNode) {
//						//cout << "\t\t" << hierarchy[h][j] << "->" << swarm.at(hierarchy[h][j])->getParent() << endl;
//						childCounter++;
//					}
//					//Check if there is a new parent
//					if (swarm.at(currentPNodeIndex)->getCurrentEvaluation() < swarm.at(parentNode)->getCurrentEvaluation()){
//						newParent = swarm.at(currentPNodeIndex)->getID();	//newParent coordinates (this is the  that becomes parent)
//						newH = h;
//						newWidth = j;
//					}
//					//Switch parent and child after comparing with all children
//					if (j == (iterCount*branching)+branching-1 && (newParent != currentPNodeID)){
//						swapNodes(newParent, newH, newWidth, parentNode, parentH, parentWidth, branching, h, width, iterCount);
//					}
//				}
//				catch (const std::exception& e) {
//					break;
//				}
//			}
//
//			iterCount++;
//		}
//		if ((long unsigned) childCounter==swarm.size())
//			break;
//	}
//}
//
//void Swarm::swapNodes(int newParent, int newH, int newWidth, int parentNode, int parentH, int parentWidth, int branching, int h, int width, int iterCount){
//	//cout << "\t\t New parent: " << newParent << " coordinates: "  << newH << " "<< newWidth << endl;
//	//cout << "\t\t Replaces: " << parentNode << " coordinates: "  << parentH << " "<< parentWidth << endl;
//
//	int childNodeIndex;
////	int parentID;
//	int parentNodeIndex;
//	int partIndex;
//	//Get particle ID and position in swarm
//	for (unsigned int l=0;l<swarm.size();l++){
//		if (swarm.at(l)->getID() == hierarchy.at(newH).at(newWidth)){
//			childNodeIndex = l;
//			break;
//		}
//	}
//	//Get particle ID and position in swarm
//	for (unsigned int l=0;l<swarm.size();l++){
//		if (swarm.at(l)->getID() == hierarchy.at(parentH).at(parentWidth)){
//			//parentID = swarm.at(l)->getParent();
//			parentNodeIndex = l;
//			break;
//		}
//	}
//
//	//Swap child and parent IDs
////	swarm.at(hierarchy.at(newH).at(newWidth))->setParent(swarm.at(hierarchy.at(parentH).at(parentWidth))->getParent());
////	swarm.at(hierarchy.at(parentH).at(parentWidth))->setParent(newParent); //parent
//
//	//Swap places in swarm
//	swarm.at(childNodeIndex)->setParent(swarm.at(parentNodeIndex)->getParent());
//	swarm.at(swarm.at(parentNodeIndex)->getParent())->setParent(newParent); //parent
//	//Swap places in hierarchy
//	hierarchy.at(parentH).at(parentWidth) = newParent;
//	hierarchy.at(newH).at(newWidth) = parentNode;
//
//	//cout << "\t\t s" << hierarchy[newH][newWidth] << "->" << swarm.at(hierarchy[newH][newWidth])->getParent() << endl;
//	//cout << "\t\t s" << hierarchy[parentH][parentWidth] << "->" << swarm.at(hierarchy[parentH][parentWidth])->getParent() << endl;
//
//	if (h <= lastLevelComplete) {
//		//Update brothers parentID
//		for (int k=width-(branching-1); k<=width; k++){
//			//Find particles position in swarm
//			for (unsigned int l=0;l<swarm.size();l++){
//				if (swarm.at(l)->getID() == hierarchy.at(h).at(k)){
//					//parentID = swarm.at(l)->getParent();
//					partIndex = l;
//					break;
//				}
//			}
//			//swarm.at(hierarchy.at(h).at(k))->setParent(newParent);
//			swarm.at(partIndex)->setParent(newParent);
//		}
//		//Update any children that the old child (now parent) may have had
//		for (int k=branching*width; k<(branching*width)+branching; k++){
//			//Find particles position in swarm
//			for (unsigned int l=0;l<swarm.size();l++){
//				partIndex = -1; //initialize to -1 so the loop breaks with the catch when there are no more childs
//				if (swarm.at(l)->getID() == hierarchy.at(h+1).at(k)){
//					//parentID = swarm.at(l)->getParent();
//					partIndex = l;
//					break;
//				}
//			}
//			try {
//				//swarm.at(hierarchy.at(h+1).at(k))->setParent(parentNode);
//				swarm.at(partIndex)->setParent(parentNode);
//				//cout << "\t\t c" << hierarchy[h+1][k] << "->" << swarm.at(hierarchy[h+1][k])->getParent() << endl;
//			}
//			catch (const std::exception& e) {
//				break;
//			}
//		}
//	}
//	if (h == lastLevelComplete+1) {
//		//Update brothers parentID
//		for(int k=iterCount*branching;k<(iterCount*branching)+branching;k++){
//			for (unsigned int l=0;l<swarm.size();l++){
//				partIndex = -1; //initialize to -1 so the loop breaks with the catch when there are no more childs
//				if (swarm.at(l)->getID() == hierarchy.at(h).at(k)){
//					//parentID = swarm.at(l)->getParent();
//					partIndex = l;
//					break;
//				}
//			}
//			try{
//				//swarm.at(hierarchy.at(h).at(k))->setParent(newParent);
//				swarm.at(partIndex)->setParent(newParent);
//				//cout << "\t\t b" << hierarchy[h][k] << "->" << swarm.at(hierarchy[h][k])->getParent() << endl;
//			}
//			catch (const std::exception& e) {
//				break;
//			}
//		}
//	}
//}
//
//void Swarm::printAllParentNodes(){
//	int parentNode = 0;
//	for(unsigned int i=0; i<swarm.size(); i++){
//		cout << "Particle " << i << ": " ;
//		parentNode = swarm.at(i)->getParent();
//
//		if (parentNode==-1){
//			cout << "Root node";
//		}
//		else{
//			while (parentNode != -1){
//				cout << parentNode << " ";
//				parentNode = swarm.at(parentNode)->getParent();
//			}
//		}
//		cout << endl ;
//	}
//}
//
//void Swarm::printTree(int branching, long swarm_size) {
//	//Raw printing of the structure
//	cout << endl << endl;
//	for (int i=0; i<=lastLevelComplete+1; i++){
//		cout << "Level " << i << ": ";
//		for (int j=0; j<pow(branching,lastLevelComplete+1); j++)
//			cout << hierarchy.at(i).at(j) << " ";
//		cout << endl;
//	}
//
//	int parentNode;
//	int parentNodeIndex;
//	int currentPNodeID;
//	int partIndex;
//	//Find particle ID and position in swarm
//	for (unsigned int i=0;i<swarm.size();i++){
//		if (swarm.at(i)->getID() == hierarchy.at(0).at(0)){
//			parentNode = swarm.at(i)->getParent();
//			parentNodeIndex = i;
//			break;
//		}
//	}
//
//	//Print tree -- this is the same algorithm we use to traverse the tree and update the position of the particles
//	cout << "\n\nRoot node: " << endl;
//	//cout << "\t\t" << hierarchy.at(0).at(0) << "->" << swarm.at(hierarchy.at(0).at(0))->getParent() << endl << "Level 1: " << endl;
//	cout << "\t\t" << swarm.at(parentNodeIndex)->getID() << "->" << parentNode << endl << "Level 1: " << endl;
//	//reset indexes to traverse the entire tree
//	int h = 1;
//	int width = 0;
//	int iterCount = 0;
//	int childCounter = 1;
//	for(unsigned int i=1;i<swarm_size;i++){
//		if (h <= lastLevelComplete){
//			if (width < pow(branching,h)){ //max level width
//				//Find particle ID and position in swarm
//				for (unsigned int l=0;l<swarm.size();l++){
//					if (swarm.at(l)->getID() == hierarchy.at(h).at(width)){
//						currentPNodeID = swarm.at(l)->getParent();
//						break;
//					}
//				}
//				if (parentNode!=currentPNodeID){
//					parentNode=currentPNodeID;
//					cout << "Parent: " << parentNode << endl;
//				}
//				cout << "\t\t" << hierarchy.at(h).at(width) << "->" << currentPNodeID << endl;
//				width++;
//				childCounter++;
//			}
//			if (width == pow(branching,h)){
//				h++;
//				width=0;
//				cout << "Level " << h << ": " << endl;
//			}
//		}
//		else {
//			parentNode = hierarchy.at(h-1).at(iterCount);
//			cout << "Parent: " << parentNode << endl;
//			for(int j=iterCount*branching;j<(iterCount*branching)+branching;j++){
//				//Find particle ID and position in swarm
//				for (unsigned int l=0;l<swarm.size();l++){
//					partIndex = -1; //initialize to -1 so the loop breaks with the catch when there are no more childs
//					if (swarm.at(l)->getID() == hierarchy.at(h).at(j)){
//						//parentID = swarm.at(l)->getParent();
//						partIndex = l;
//						break;
//					}
//				}
//				if (partIndex == -1) break;
//				try{
//					if (swarm.at(partIndex)->getParent() == parentNode) {
//						cout << "\t\t" << hierarchy.at(h).at(j) << "->" << swarm.at(partIndex)->getParent() << endl;
//						childCounter++;
//					}
//				}
//				//This exception is thrown when hierarchy[][] = -2 and we try to access a position of swarm.at(-2), which does not exist
//				catch (const std::exception& e) {
//					//cout << "h[" << h << "] j[" << j << "] iter " << iterCount << endl;
//					break;
//				}
//			}
//			iterCount++;
//		}
//		if ((unsigned)childCounter==swarm.size())
//			break;
//	}
//}

void Swarm::clearResizeSimpSwarm(Configuration* config, long int iteration){
	//	if ((iteration > 1 &&
	//			((config->getOmega1CS() == IW_SUCCESS_BASED || config->getOmega1CS() == IW_CONVERGE_BASED) ||
	//					(config->getAngleCS() == ANGLE_ADAPTIVE &&
	//							(config->getRandomMatrix() == MATRIX_RRM_EXP_MAP || config->getRandomMatrix() == MATRIX_RRM_EUCLIDEAN_ONE ||
	//									config->getRandomMatrix() == MATRIX_RRM_EUCLIDEAN_ALL)
	//					)
	//			)
	//	) || (iteration == 1 && (config->getOmega1CS() == IW_CONVERGE_BASED || config->getOmega1CS() == IW_SUCCESS_BASED)) ){
	//Copy particle's id and evaluation in simpSwarm for the next iteration
	simpSwarm.clear();
	simpSwarm.resize(swarm.size());
	for (unsigned int i=0;i<simpSwarm.size();i++){
		simpSwarm.at(i).id = swarm.at(i)->getID();
		simpSwarm.at(i).eval = swarm.at(i)->getCurrentEvaluation();
	}
}

// This function computes the inertia weight (omega1 in the GVU formula) according to the selected strategy
double Swarm::computeOmega1(Configuration* config, long int iteration, long int id, bool newIteration){
	//If all particle use the same inertia value, it is more efficient
	//to compute it once at the beginning of the iteration
	if (newIteration) {
		/* Non-adaptive strategies */
		//IW_CONSTANT - 0 - Constant value
		if (config->getOmega1CS() == IW_CONSTANT){
			if (config->getOmega1() < -1 || config->getOmega1() > 1) //check convergence bounds
				config->setOmega1(CONSTRICTION_COEFFICIENT);
			//cout << "\tvar::Omega1: " << config->getOmega1() << " ";
		}
		//IW_L_INC - 1 - Linear increasing
		else if (config->getOmega1CS() == IW_L_INC) {
			if (config->getIWSchedule() > 0 ){
				//from Frankenstein's PSO
				if(iteration <= config->getIWSchedule()){
					config->setOmega1(config->getInitialIW() +
							(((double)(config->getIWSchedule() - iteration)/config->getIWSchedule())*
									(config->getFinalIW() - config->getInitialIW()))
					);
					//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
				}
				else
					config->setOmega1(config->getFinalIW());
			}
			else{
				config->setOmega1( config->getFinalIW() +
						(((double)(iteration)/config->getMaxIterations())*
								(config->getInitialIW()-config->getFinalIW()))
				);
			}
		}
		//IW_L_DEC - 2 - Linear decreasing
		else if (config->getOmega1CS() == IW_L_DEC) {
			if (config->getIWSchedule() > 0 ){
				//from Frankenstein's PSO
				if(iteration <= config->getIWSchedule()){
					config->setOmega1( config->getFinalIW() +
							(((double)(config->getIWSchedule() - iteration)/config->getIWSchedule())*
									(config->getInitialIW() - config->getFinalIW()))
					);
					//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
				}
				else
					config->setOmega1(config->getFinalIW());
			}
			else {
				config->setOmega1( config->getInitialIW() +
						(((double)(iteration)/config->getMaxIterations())*
								(config->getFinalIW()-config->getInitialIW()))
				);
			}
		}
		//IW_RANDOM - 3 - Random
		else if (config->getOmega1CS() == IW_RANDOM) {
			config->setOmega1( 0.5 * (problem->getRandom01()/2.0));
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_NONL_DEC - 4 - Nonlinear decreasing
		else if (config->getOmega1CS() == IW_NONL_DEC) {
			config->setOmega1(
					config->getFinalIW() + ((config->getInitialIW()-config->getFinalIW())*
							pow((double)(iteration)/config->getMaxIterations(),alpha))
			);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_NONL_DEC_IMP - 5 - Nonlinear decreasing improved
		else if (config->getOmega1CS() == IW_NONL_DEC_IMP) {
			config->setOmega1(
					pow(omegaXu, iteration)
			);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_NONL_DEC_TIME - 6 - Nonlinear decreasing time-dependent
		else if (config->getOmega1CS() == IW_NONL_DEC_TIME) {
			config->setOmega1(
					pow((2.0/iteration), 0.3)
			);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_CHAOTIC_DEC - 7 Chaotic decreasing
		else if (config->getOmega1CS() == IW_CHAOTIC_DEC) {
			iteration == 1 ? zFunction = problem->getRandom01() : zFunction = 4*zFunction*(1-zFunction);
			config->setOmega1(
					(zFunction*config->getInitialIW()) + (config->getFinalIW()-config->getInitialIW()) *
					((double)(config->getMaxIterations())-iteration)/config->getMaxIterations()
			);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_EXP_DEC - 8 - Natural exponential decreasing
		else if (config->getOmega1CS() == IW_EXP_DEC) {
			config->setOmega1(
					config->getInitialIW() + (config->getFinalIW()-config->getInitialIW())*
					exp((-10.0*iteration)/config->getMaxIterations())
			);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_OSCILLATING - 9 - Oscillating
		else if (config->getOmega1CS() == IW_OSCILLATING) {
			if (iteration < (3*config->getMaxIterations())/4)
				config->setOmega1(
						((config->getInitialIW() + config->getFinalIW()) /2.0) +
						((config->getFinalIW() + config->getInitialIW()) /2.0) *
						cos((simNumOfCos*iteration)/(config->getMaxIterations()*3.0))
				);
			else
				config->setOmega1(config->getInitialIW());
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_LOG_DEC - 10 - Logarithm decreasing
		else if (config->getOmega1CS() == IW_LOG_DEC) {
			config->setOmega1(
					config->getFinalIW() + (config->getInitialIW()-config->getFinalIW())*
					log10(((10.0*iteration)/config->getMaxIterations())+ a )
			);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}

		/* ****************************************************************************************************************/
		/* ****************************************************************************************************************/

		/* Adaptive strategies */
		//IW_SELF_REGULATING - 11 - Self-regulating
		else if (config->getOmega1CS() == IW_SELF_REGULATING) {
			static double deltaOmega = (((double)config->getFinalIW()) - config->getInitialIW())/config->getMaxIterations();
			iteration == 1 ? omega_2 = config->getFinalIW() : omega_2 = omega_2-deltaOmega;
			config->setOmega1(omega_2);
			//cout << iteration << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_VELOCITY_BASED - 12 - Based on velocity information
		else if (config->getOmega1CS() == IW_VELOCITY_BASED) {
			static double T_0_95 = 95*config->getMaxIterations()/100; //iteration at which 95% of search process is completed
			double lambda = config->get_iw_par_deltaOmega();
			if (iteration == 1)
				config->setOmega1(config->getFinalIW());

			idealVelocity = swarm.at(0)->getMaxVelLimit() * ((1.0 + cos(M_PI*(iteration/T_0_95)))/2);
			avVel=computeAvgVelocity(config);	//average absolute velocity of the swarm

			if (avVel >= idealVelocity){
				(config->getOmega1()-lambda) >= config->getInitialIW() ?
						config->setOmega1(config->getOmega1()-lambda) : config->setOmega1(config->getInitialIW());
			}
			else{
				(config->getOmega1()+lambda) >= config->getFinalIW() ?
						config->setOmega1(config->getFinalIW()) : config->setOmega1(config->getOmega1()+lambda);
			}
			//cout << iteration << endl;
			//cout << avVel << " -- " << idealVelocity << endl;
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_RANKS_BASED - 14 - Rank-based
		else if (config->getOmega1CS() == IW_RANKS_BASED) {
			//simpSwarm.clear();
			//simpSwarm.resize(swarm.size());
			//try {// vector::at throws an out-of-range
			//rankParticles(simpSwarm);
			//}
			//catch (const std::out_of_range& oor) {
			//simpSwarm.clear();
			//simpSwarm.resize(swarm.size());
			//rankParticles(simpSwarm);
			//cerr << "Out of Range error: " << oor.what() << '\n';
			//cout << "\tnotice::swarm.size() = " << swarm.size() << endl;
			//cout << "\tnotice::simpSwarm.size() = " << simpSwarm.size() << endl;
			rankParticles(simpSwarm);
			//			}

		}
		//IW_SUCCESS_BASED - 15 Success-based
		else if (config->getOmega1CS() == IW_SUCCESS_BASED) {
			//if ((iteration == 1 && config->getPopulationCS() == POP_CONSTANT) || config->getPopulationCS() != POP_CONSTANT){
			if (iteration == 1){
				//simpSwarm contains a simplified copy of the swarm at t-1
				//				simpSwarm.clear();
				//				simpSwarm.resize(swarm.size());
				//				for (unsigned int i=0;i<swarm.size();i++){
				//					simpSwarm.at(i).id = swarm.at(i)->getID();
				//					simpSwarm.at(i).eval = swarm.at(i)->getCurrentEvaluation();
				//				}
				//clearResizeSimpSwarm(config, iteration);
				config->setOmega1(config->getFinalIW()); //set inertia weight to its maximum value for the first iteration
			}
			if (iteration > 1){
				int S_i = 0; //Number of solutions that improved after the last iteration
				for (unsigned int i=0; i<swarm.size(); i++){
					for (unsigned int j=0; j<simpSwarm.size(); j++){
						if (swarm.at(i)->getID() == simpSwarm.at(j).id){
							//evaluate if the solution improved
							if (swarm.at(i)->getCurrentEvaluation() < simpSwarm.at(j).eval){
								S_i++;
								break;
							}
							else
								break;
						}
					}
				}
				//set the inertia weight
				config->setOmega1( config->getInitialIW() + ((config->getFinalIW()-config->getInitialIW()) *
						((double)S_i/swarm.size()))
				);
				//				//Copy particle's id and evaluation in simpSwarm for the next iteration
				//				simpSwarm.clear();
				//				simpSwarm.resize(swarm.size());
				//				for (unsigned int i=0;i<swarm.size();i++){
				//					simpSwarm.at(i).id = swarm.at(i)->getID();
				//					simpSwarm.at(i).eval = swarm.at(i)->getCurrentEvaluation();
				//				}
				//cout << iteration << endl;
				//cout << "P[" << id << "]" << "rank: "<< ranking << " eval:" << current.eval << endl;
			}
			//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
		}
		//IW_CONVERGE_BASED - 16 Convergence-based
		else if (config->getOmega1CS() == IW_CONVERGE_BASED) {
			//if ((iteration == 1 && config->getPopulationCS() == POP_CONSTANT) || config->getPopulationCS() != POP_CONSTANT){
			if (iteration == 1 ){
				double alpha_2 = config->get_iw_par_alpha_2();
				double beta_2 = config->get_iw_par_beta_2();
				//simpSwarm contains a simplified copy of the swarm at t-1
				//				simpSwarm.clear();
				//				simpSwarm.resize(swarm.size());
				//				//Copy particle's id and evaluation in simpSwarm
				//				for (unsigned int i=0;i<swarm.size();i++){
				//					simpSwarm.at(i).id = swarm.at(i)->getID();
				//					simpSwarm.at(i).eval = swarm.at(i)->getCurrentEvaluation();
				//				}
				clearResizeSimpSwarm(config, iteration);
				if (config->getPopulationCS() == POP_CONSTANT)
					config->setOmega1(1 - fabs(alpha_2/(1+beta_2)));
			}
			//			else{
			//				simpSwarm.clear();
			//				simpSwarm.resize(swarm.size());
			//				//Copy particle's id and evaluation in simpSwarm for the next iteration
			//				for (unsigned int i=0;i<swarm.size();i++){
			//					simpSwarm.at(i).id = swarm.at(i)->getID();
			//					simpSwarm.at(i).eval = swarm.at(i)->getCurrentEvaluation();
			//				}
			//			}
		}
		//Keep inertia constant during the execution
		else {
			config->setOmega1(config->getOmega1()); //kind of unnecessary, but ensures integrity
			//cout << inertia << endl;
			//return inertia;
		}
		//if (config->verboseMode()) cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
	}
	else {
		//These are the strategies that need to compute a independent inertia value for each particle
		//Note that the variables used here are computed/allocated when (newIteration == true).
		if (id != -1){
			double temp_Omega = 0;

			switch (config->getOmega1CS()) {
			//IW_L_INC - 1 - Linear increasing
			case IW_L_INC:{
				if (config->getTopology() == TOP_HIERARCHICAL){
					//From Hierarchical PSO
					double k=((double)getParticleNumParents(id));
					config->setOmega1( config->getFinalIW() +
							( ((config->getInitialIW()-config->getFinalIW()) * k) / (lastLevelComplete+1))
					);
					if (config->verboseMode()) cout << "\tvar:w_max: " << config->getInitialIW() << "\n";
					if (config->verboseMode()) cout << "\tvar:w_min: " << config->getFinalIW() << "\n";
					if (config->verboseMode()) cout << "\tvar::(k)partents_counter: " << k << "\n";
					if (config->verboseMode()) cout << "\tvar::(h)lastLevelComplete: " << lastLevelComplete << "\n";
					temp_Omega = config->getOmega1();
				}
				else
					temp_Omega = config->getOmega1();
			}
			break;
			//IW_L_DEC - 2 - Linear decreasing
			case IW_L_DEC: {
				if (config->getTopology() == TOP_HIERARCHICAL){
					//From Hierarchical PSO
					double k=((double)getParticleNumParents(id));
					config->setOmega1( config->getInitialIW() +
							( ((config->getFinalIW()-config->getInitialIW()) * k)/(lastLevelComplete+1))
					);
					if (config->verboseMode()) cout << "\tvar:w_max: " << config->getInitialIW() << "\n";
					if (config->verboseMode()) cout << "\tvar:w_min: " << config->getFinalIW() << "\n";
					if (config->verboseMode()) cout << "\tvar::(k)partents_counter: " << k << "\n";
					if (config->verboseMode()) cout << "\tvar::(h)lastLevelComplete: " << lastLevelComplete << "\n";
				}
				else
					temp_Omega = config->getOmega1();
			}
			break;
			//IW_SELF_REGULATING - 11 - Self-regulating
			case IW_SELF_REGULATING:{
				double eta = config->get_iw_par_eta();
				//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
				//cout << "\tvar::eta: " << eta << "\n";
				//cout << "\tvar::id: " << id << "\n";
				//cout << "\tvar::best_particle->getID(): " << best_particle->getID() << "\n";
				//The best particle has a special inertia value
				if (id == best_particle->getID())
					temp_Omega = omega_2 + eta * ((config->getFinalIW() - config->getInitialIW()) / config->getMaxIterations());
				else
					temp_Omega = config->getOmega1();
			}
			break;
			//IW_DOUBLE_EXP - 13 - Double exponential self-adaptive
			case IW_DOUBLE_EXP:{
				double R =0.0;
				if (iteration == 1){
					config->setOmega1(config->getFinalIW());
					//cout << id << endl;
				}
				else {
					R = swarm.at(id)->computeDistPbestGbest()*((((double)config->getMaxIterations())-iteration)/config->getMaxIterations());
					config->setOmega1( exp(-1*exp((R*-1))));
					//cout << id << endl;
				}
				//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
				temp_Omega = config->getOmega1();
			}
			break;
			//IW_RANKS_BASED - 14 - Rank-based
			case IW_RANKS_BASED:{
				config->setOmega1( config->getInitialIW() + ((config->getFinalIW()-config->getInitialIW()) *
						((double) swarm.at(id)->getRanking()/config->getSwarmSize()))
				);
				//cout << id << endl;
				//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
				temp_Omega = config->getOmega1();
			}
			break;
			//IW_CONVERGE_BASED - 16 Convergence-based
			case IW_CONVERGE_BASED:{
				if (iteration > 1) {
					double alpha_2 = config->get_iw_par_alpha_2();
					double beta_2 = config->get_iw_par_beta_2();

					//Find the particle in simpSwarm
					for (unsigned int j=0; j<simpSwarm.size(); j++){
						if (simpSwarm.at(j).id == swarm.at(id)->getID()){
							//convergence factor
							long double c_i = fabs(simpSwarm.at(j).eval - swarm.at(id)->getCurrentEvaluation())/
									(simpSwarm.at(j).eval + swarm.at(id)->getCurrentEvaluation());
							//diffusion factor
							long double d_i = fabs(swarm.at(id)->getCurrentEvaluation() - global_best.eval) /
									(swarm.at(id)->getCurrentEvaluation() + global_best.eval);
							//set the inertia weight
							config->setOmega1(1 - fabs(alpha_2*(1-c_i)) / (1+d_i)*(1+beta_2));
							break;
						}
					}
					//cout << id << endl;
					//cout << "\tvar::Omega1: " << config->getOmega1() << "\n";
					temp_Omega = config->getOmega1();
				}
			}
			break;
			default:
				temp_Omega = config->getOmega1();
				break;
			}
			if (config->verboseMode()) cout << "\t\tvar::Omega1: " << config->getOmega1() << " \n";
			return temp_Omega;
		}
		else{
			cerr << "Unexpected error while computing the value of inertia: id=-1 is not valid" << endl;
			exit (-1);
		}
	}
	return config->getOmega1();
}


double Swarm::computeOmega2(Configuration* config){
	switch(config->getOmega2CS()){
	case O2_EQUAL_TO_O1:
		return config->getOmega1();
	case O2_RANDOM:
		return 0.5 + (problem->getRandom01()/2.0);
	case O2_ZERO:
		return 0.0; //the component is not used
	case O2_CONSTANT:
		return config->getOmega2();
	default :
		return 1.0; //no strategy, set the value to one
	}
}

double Swarm::computeOmega3(Configuration* config){
	//Same as Omega1
	if (config->getOmega3CS() == O3_EQUAL_TO_O1)
		return config->getOmega1();
	//Random value
	else if (config->getOmega3CS() == O3_RANDOM)
		return 0.5 + (problem->getRandom01()/2.0);
	//Zero -- component is not being used
	else if (config->getOmega3CS() == O3_ZERO)
		return 0.0;
	//Constant
	else if (config->getOmega3CS() == O3_CONSTANT)
		return config->getOmega3();
	//One -- no strategy in particular
	else
		return 1.0;
}

bool Swarm::isHierarchical(){
	return hierarchical;
}

double Swarm::computeAvgVelocity(Configuration* config){
	double sumVelocity = 0.0;
	for (unsigned int i=0;i<swarm.size();i++){
		for (unsigned int j=0;j<config->getProblemDimension();j++){
			sumVelocity = sumVelocity + fabs(swarm.at(i)->getCurrentVelocity()[j]);
		}
	}
	return (sumVelocity/(swarm.size()*config->getProblemDimension()));
}

void Swarm::rankParticles(vector<SimplifySwarm> &sSwarm){
	//The ranking are obtained via the mergesort algorithm implemented in Utils.cpp

	ssArray sS;
	sS.id = new int [sSwarm.size()];
	sS.eval = new long double [sSwarm.size()];

	//Copy particle's id and evaluation in simpSwarm
	//cout << "\n\tSwarm before ranks:" << endl;
	for (unsigned int i=0;i<sSwarm.size();i++){
		sS.id[i] = swarm.at(i)->getID();
		sS.eval[i] = swarm.at(i)->getCurrentEvaluation();
		//cout << "ID: " << sS.id[i] << " eval: " << sS.eval[i] << " rank: " << swarm.at(i)->getRanking() << endl;
	}
	//Sort simpSwarm by the value of particles' evaluation
	mergeSort(&sS, 0, sSwarm.size()-1); //mergeSort(array, left(LOWER) index, right (UPPER) index);


	//Set particles' rank in the swarm
	for (unsigned int i=0;i<sSwarm.size();i++){
		for (unsigned int j=0;j<swarm.size();j++)
			if (swarm.at(j)->getID() == sS.id[i]){
				swarm.at(i)->setRanking(i+1);
				sSwarm.at(i).id = sS.id[i];
				sSwarm.at(i).eval = swarm.at(j)->getCurrentEvaluation();
			}
	}
	//for (unsigned int i=0;i<swarm.size();i++){
	//cout << "ID: " << sSwarm.at(i).id << " eval: " << sSwarm.at(i).eval << " rank: " << swarm.at(i)->getRanking() << endl;
	//}

	delete [] sS.id;
	delete [] sS.eval;
}

//mergeSort(arr, 0, arr_size-1);
void Swarm::mergeSort(ssArray* arr, int l, int r){
	if (l < r) {
		// Middle point of the array
		int m = l+(r-l)/2;

		// Sort first and second halves
		mergeSort(arr, l, m);
		mergeSort(arr, m+1, r);
		merge(arr, l, m, r);
	}
}

// Merges two subarrays of arr[].
// First subarray is arr[l..m]
// Second subarray is arr[m+1..r]
void Swarm::merge(ssArray* arr, int l, int m, int r){
	int i, j, k;
	int n1 = m - l + 1;
	int n2 =  r - m;

	/* create temp arrays */
	//struct SimplifySwarm L[n1], R[n2];
	//struct SimplifySwarm L, R;
	long double L_eval [n1];
	int L_id [n1];
	long double R_eval [n2];
	int R_id [n2];

	/* Copy data to temp arrays L[] and R[] */
	for (i = 0; i < n1; i++){
		L_eval[i] = arr->eval[l + i];
		L_id[i] = arr->id[l + i];
	}
	for (j = 0; j < n2; j++){
		R_eval[j] = arr->eval[m + 1+ j];
		R_id[j] = arr->id[m + 1+ j];
	}

	/* Merge the temp arrays back into arr[l..r]*/
	i = 0; // Initial index of first subarray
	j = 0; // Initial index of second subarray
	k = l; // Initial index of merged subarray
	while (i < n1 && j < n2) {
		if (L_eval[i] <= R_eval[j]) {
			arr->eval[k] = L_eval[i];
			arr->id[k] = L_id[i];
			i++;
		}
		else {
			arr->eval[k] = R_eval[j];
			arr->id[k] = R_id[j];
			j++;
		}
		k++;
	}

	/* Copy the remaining elements of L[], if there
       are any */
	while (i < n1) {
		arr->eval[k] = L_eval[i];
		arr->id[k] = L_id[i];
		i++;
		k++;
	}

	/* Copy the remaining elements of R[], if there
       are any */
	while (j < n2) {
		arr->eval[k] = R_eval[j];
		arr->id[k] = R_id[j];
		j++;
		k++;
	}
}
