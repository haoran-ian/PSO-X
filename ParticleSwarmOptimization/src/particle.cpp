/*
 * particle.cpp
 *
 *  Created on: May 31, 2018
 *      Author: leonardo
 */

#include "float.h"
#include "particle.h"
#include "iostream"
#include "rng.h"
#include <math.h>
#include "utils.h"


using namespace std;

double additionalVal= 0.0; //variable in case some additional value has to be use to update the formula
double prev_D = 0.0;		//dispersion value used for normal distribution

/* Default constructor*/
Particle::Particle(){
	id = 0;
	phi_1 = 0;
	phi_2 = 0;
	inertia = 0;
	//velocity = {0};
	velocity = 0;
	size = 0;
	problem = 0;
	init=false;
	hasVelocitybounds = false;
	minVelLimit = -100;					//minimum velocity -v_max = (x_max-x_min)/2
	maxVelLimit = 100;					//maximum velocity  v_max
	ranking = 0;
	parent =0;
	stereotype = 0;
	perturbationVal = 0;
}

/* Constructor*/
Particle::Particle (Problem* problem, Configuration* config, int identifier){

	this->problem = problem;
	size= problem->getProblemDimension();
	id = identifier;
	ranking = 0;
	parent = 0;
	stereotype = 0;
	perturbationVal = 0;

	hasVelocitybounds = config->useVelocityClamping();
	setVelocityLimits(config); //Set velocity clamping limits

	current.x = new double[size];
	pbest.x = new double[size];
	gbest.x = new double[size];
	current.eval = LDBL_MAX;
	pbest.eval = LDBL_MAX;
	gbest.eval = LDBL_MAX;

	velocity = new double[size];

	phi_1 = config->getPhi1();
	phi_2 = config->getPhi1();
	inertia = config->getInertia();

	//cout << "Particle " << id << " : [ ";
	initializeUniform();

	//printPosition();
	init = true;
}

/* Copy constructor */
Particle::Particle (const Particle &p){
	problem = p.problem;
	size = p.size;
	id = p.id;
	ranking = p.ranking;
	parent = p.parent;
	stereotype = p.stereotype;
	perturbationVal = p.perturbationVal;

	hasVelocitybounds = p.hasVelocitybounds;
	minVelLimit = p.minVelLimit;
	maxVelLimit = p.maxVelLimit;

	if(!init){
		current.x = new double[size];
		pbest.x = new double[size];
		gbest.x = new double[size];
		velocity = new double[size];
	}

	for(int i=0; i<size;i++){
		current.x[i] = p.current.x[i];
		pbest.x[i] = p.pbest.x[i];
		gbest.x[i] = p.gbest.x[i];
		velocity[i] = p.velocity[i];
	}

	current.eval = p.current.eval;
	pbest.eval = p.pbest.eval;
	gbest.eval = p.gbest.eval;

	inertia=p.inertia;
	phi_1 = p.phi_1;
	phi_2 = p.phi_2;
	init = true;

}

/* destructor */
Particle::~Particle(){
	if(init){
		delete[] current.x;
		delete[] pbest.x;
		delete[] gbest.x;
		delete[] velocity;
	}
	init=false;
}

/* overriding of '=' operator for particles
   (now 'p1 = p2;' does what one would expect) */
Particle& Particle::operator= (const Particle& p){
	if (this != &p) {
		problem = p.problem;
		size = p.size;
		id = p.id;
		ranking = p.ranking;
		parent = p.parent;
		stereotype = p.stereotype;
		perturbationVal = p.perturbationVal;

		hasVelocitybounds = p.hasVelocitybounds;
		minVelLimit = p.minVelLimit;
		maxVelLimit = p.maxVelLimit;

		if(!init){
			current.x = new double[size];
			pbest.x = new double[size];
			gbest.x = new double[size];
			velocity = new double[size];
		}

		for(int i=0; i<size;i++){
			current.x[i] = p.current.x[i];
			pbest.x[i] = p.pbest.x[i];
			gbest.x[i] = p.gbest.x[i];
			velocity[i] = p.velocity[i];
		}

		current.eval = p.current.eval;
		pbest.eval = p.pbest.eval;
		gbest.eval = p.gbest.eval;

		inertia=p.inertia;
		phi_1 = p.phi_1;
		phi_2 = p.phi_2;
		init = true;
	}
	return *this;
}

/* Initialize particle */
void Particle::initializeUniform(){
	for(int i=0; i<size; i++){
		current.x[i] = problem->getRandomX(); //random values within the bounds of the function
		pbest.x[i]=current.x[i];
		velocity[i]=0;
	}
	computeEvaluation();
}

void Particle::setVelocityLimits(Configuration* config){
	if((config->getProblemID() == 6 || config->getProblemID() == 24) && (config->getCompetitionID() == 0)){
		minVelLimit= (LDBL_MAX)/-2;
		maxVelLimit= (LDBL_MAX)/2;
	}
	else {
		minVelLimit=((config->getMaxInitRange()-config->getMinInitRange())/2.0)*-1;
		maxVelLimit=((config->getMaxInitRange()-config->getMinInitRange())/2.0);
	}
}

double Particle::getMinVelLimit(){
	return minVelLimit;
}

double Particle::getMaxVelLimit(){
	return maxVelLimit;
}

int Particle::getRanking(){
	return ranking;
}

void Particle::setRanking(int rank){
	ranking = rank;
}

int Particle::getParent(){
	return parent;
}

void Particle::setParent(int node){
	parent = node;
}

/* Generate a new solution by updating the particle's position */
void Particle::move(Configuration* config, double minBound, double maxBound, long int iteration,
		double omega1, double omega2, double omega3, int numInformants, int *theInformants, int lastLevelComplete,
		double alpha_t, double l, double delta){
	//For VEL_LINEAR all entries of the random matrix are the same
	double u1=problem->getRandom01(); //random value for the personal component
	double u2=problem->getRandom01(); //random value for the social component
	double V2[size];
	double V1[size];

	if (config->getVelocityRule() == VEL_STANDARD2011){
		//random vector from a hyperspherical distribution with center G and radius G-X
		getHypersphericalVector(V2, V1);
	}

	//cout << "\n alpha_t recived is: " << alpha_t << endl;
	//perturbationVal = computePerturbation(config, current.x, neighbours[0]->current.x, alpha_t, l, delta, true);
	cout << "\n perturbationVal: "  << scientific << perturbationVal << endl;

	//Compute new position
	for (int i=0;i<size;i++) {
		//TODO: Here I could modify this to determine the parameters sent to computeNewVelocity() using a switch statement
		perturbationVal = computePerturbation(config, current.x, neighbours[0]->current.x, alpha_t, l, delta, false);
		//cout << "\n perturbationVal: "  << scientific << perturbationVal << endl;

		double PersonalInfluence = pbest.x[i]-current.x[i];
		double SocialInfluence = gbest.x[i]-current.x[i];

		if (config->getVelocityRule() == VEL_STANDARD2011)
			additionalVal = V2[i] + V1[i];

		if (config->getVelocityRule() == VEL_GUARAN_CONVERG){
			//if (pbest.eval == gbest.eval)
		}
		velocity[i] = computeNewVelocity(config, velocity[i], u1, u2, PersonalInfluence, SocialInfluence, current.x[i], additionalVal);

		//Check velocity clamping bounds
		if (hasVelocitybounds) {
			if (velocity[i] > maxVelLimit)
				current.x[i] = current.x[i] + maxVelLimit;
			else if (velocity[i] < minVelLimit)
				current.x[i] = current.x[i] + minVelLimit;
			else
				current.x[i] = current.x[i] + velocity[i];
		}
		else
			current.x[i] = current.x[i] + velocity[i];

		//values assigned to the variables cannot be outside the function bounds
		if(current.x[i] < minBound)
			current.x[i] = minBound;
		if(current.x[i] > maxBound)
			current.x[i]= maxBound;
	}
	computeEvaluation(); //evaluate the objective function and update pbest if a new pbest has been found
}

double Particle::computePerturbation(Configuration* config, double * pos_x, double * pbest_x, int alpha_t,
		double l, double delta, bool newInformant){
	//return value
	double returnVal = 0.0;

	switch(config->getPerturbation()){
	case PERT_ADD_RECT:
		returnVal = alpha_t*(1-(2*problem->getRandom01()));
		break;
	case PERT_DIST_NORMAL:
		if (newInformant){
			double distance = computeDistance(pos_x, pbest_x);
			if (distance == 0)
				returnVal = this->perturbationVal;
			else
				returnVal = l*distance;
		}
		else
			returnVal = this->perturbationVal;
		break;
	case PERT_ADD_NOISY:
		returnVal = problem->getRandomX(-delta/2,delta/2);
		break;
	}

	return returnVal;
}

double Particle::computeNewVelocity(Configuration* config, double vel, double u1, double u2, double perInf, double socInf, double pos, double additionalVal){
	double new_vel = 0.0;

	//Configurable PSO velocity equation
	//Constriction coefficient PSO
	//if (config->getVelocityRule() == VEL_CONSTRICTED){
	new_vel = CONSTRICTION_COEFFICIENT * (
			(inertia * vel) +
			(phi_1 * problem->getRandom01() * perInf) +
			(phi_2 * problem->getRandom01() * socInf)
	);
	//}


	return new_vel;
}

// The computation of the radius and the random point in the HyperSphere
// was taken from the publicly available from Maurice Clerc - Standard PSO 2011
// https://www.particleswarm.info/Programs.html
void Particle::getHypersphericalVector(double V2[], double V1[]){
	double G[size];	//center of the sphere
	double l[size]; //particle's Gbest
	double radius = 0.0;	//radius G-X
	double pw=1./(double)size;

	//check that Pbest != Gbest
	if (pbest.eval == gbest.eval){
		//use a random neighbor as Gbest
		int randNeighbor = getRandomNeighbor();
		for (int i=0; i<size; i++)
			l[i] = neighbours.at(randNeighbor)->current.x[i];
	}
	else
		for (int i=0; i<size; i++)
			l[i] = gbest.x[i];

	//Compute G (center of the sphere) and V1 (radius of each dimension)
	for (int i=0; i<size; i++){
		double P = current.x[i] + (phi_1 * (pbest.x[i]-current.x[i]));
		double L = current.x[i] + (phi_2 * (l[i]-current.x[i]));
		G[i] = (current.x[i] + P + L)/3.0;
		radius += pow(abs(current.x[i] - G[i]), 2);
		V1[i] = G[i] - current.x[i];
	}
	radius = sqrt(radius); //this is the actual radius of the hyper-sphere

	// Get a random vector in the hyper-sphere H(G||G-X||)
	// ----------------------------------- Step 1.  Direction
	double length=0.0;
	for (int i=0;i<size;i++) {
		V2[i] = RNG::randGauss(1.0);
		length += pow(V2[i],2);
	}
	length=sqrt(length);
	//----------------------------------- Step 2. Random radius
	// Random uniform distribution on the sphere
	double r = pow(RNG::randVal(0.0,1.0),pw);

	//----------------------------------- Step 3. Random vector
	for (int i=0;i<size;i++) {
		V2[i] = radius*r*V2[i]/length;
	}
}

void Particle::computeEvaluation() {
	//cout << "Evaluation: ";
	current.eval = problem->getFunctionValue(current.x);
	//cout << current.eval << endl;
	if (current.eval < pbest.eval) {
		for (int i=0;i<size;i++) {
			pbest.x[i] = current.x[i];
		}
		pbest.eval=current.eval;
	}
}

/* update global best solution
   INPUT: * coefficient vector x
 * corresponding solution value eval = f(x)
 */
void Particle::updateGlobalBest(double* x, double eval){
	for(int j=0;j <size;j++){
		gbest.x[j]= x[j];
	}
	gbest.eval = eval;
}

double Particle::computeDistPbestGbest(){
	double sumDistSqr = 0.0;
	for(int i=0;i<size;i++){
		sumDistSqr += pow((this->gbest.x[i]-this->pbest.x[i]),2);
	}
	return sqrt(sumDistSqr);
}

double Particle::computeDistance(double * x, double * p){
	double sumDistSqr = 0.0;
	for(int i=0;i<size;i++){
		sumDistSqr += pow((x[i]-p[i]),2);
	}
	return sqrt(sumDistSqr);
}

double* Particle::getCurrentPosition() {
	return(current.x);
}

long double Particle::getCurrentEvaluation(){
	return(current.eval);
}

double* Particle::getCurrentVelocity(){
	return velocity;
}


double* Particle::getPbestPosition() {
	return(pbest.x);
}

long double Particle::getPbestEvaluation(){
	return(pbest.eval);
}

void Particle::printPosition(){
	cout << "Solution: " << current.eval << " Id: " << id << endl;
	for(int i=0; i<size; i++){
		cout << current.x[i] << "  ";
	}
	cout << endl;

}

void Particle::printNeighborByID(int identifier){
	cout << "Solution: " << neighbours.at(identifier)->current.eval << " Id: " << neighbours.at(identifier)->id << endl;
	for(int i=0; i<size; i++){
		cout << neighbours.at(identifier)->current.x[i] << "  ";
	}
	cout << endl;
}


/*Check the neighborhood for the best particle */
void Particle::checkNeibourhood(){
	double aux_eval;
	int best=-1;

	//Check first personal best
	if(pbest.eval < gbest.eval){
		updateGlobalBest(pbest.x, pbest.eval);
	}
	//Check after the rest of the neighborhood
	aux_eval=gbest.eval;
	for(unsigned int i=0; i<neighbours.size();i++){
		if(aux_eval > neighbours[i]->getPbestEvaluation()){
			best = i;
		}
	}

	//New best particle in the neighborhood
	if(best!=-1)
		updateGlobalBest(neighbours[best]->getPbestPosition(), neighbours[best]->getPbestEvaluation());
}

/*Check the neighborhood for the best particle */
int Particle::getBestOfNeibourhood(){
	double aux_eval;
	int best=-1;

	//Check first personal best
	if(this->pbest.eval < this->gbest.eval){
		updateGlobalBest(this->pbest.x, this->pbest.eval);
	}
	//Check after the rest of the neighborhood
	aux_eval=this->gbest.eval;
	for(unsigned int i=0; i<this->neighbours.size();i++){
		if(aux_eval > this->neighbours[i]->getPbestEvaluation()){
			best = i;
		}
	}

	//New best particle in the neighborhood
	if(best!=-1)
		updateGlobalBest(this->neighbours[best]->getPbestPosition(), this->neighbours[best]->getPbestEvaluation());
	return best;
}

int Particle::getRandomNeighbor(){
	int randomIndex = -1;
	for (unsigned int i=0; i<neighbours.size(); i++) {
		randomIndex = (int)floor(RNG::randVal(0.0,(double)neighbours.size()));

		//prints
		//cout << "randomIndex: " << randomIndex << endl;//remove
		//printPosition(); //remove
		//printNeighborByID(randomIndex);//remove

		//Distinct of itself
		if (neighbours.at(randomIndex)->id != id)
			break;
		else
			continue;
	}
	if (randomIndex == -1){
		cout << "Couldn't find any :/" << endl;
		exit (EXIT_FAILURE);
		return id;
	}
	else
		return randomIndex;
}


void Particle::addNeighbour(Particle* p){
	neighbours.push_back(p);
}

//From Frankesntein's PSO
unsigned int Particle::getNeighborhoodSize(){
	return neighbours.size();
}

int Particle::getID(){
	return id;
}

void Particle::eraseNeighborbyID(int nid){
	for(unsigned int i=0;i<neighbours.size();i++){
		if(nid == neighbours.at(i)->id){
			neighbours.erase(neighbours.begin()+i);
			break;
		}
	}
}

int Particle::getRandomNonAdjacentNeighborID(Configuration* config){
	int randomIndex,randomID;
	if(neighbours.size()>3){
		while(true){
			randomIndex = (int)floor(RNG::randVal(0.0,(double)neighbours.size()));
			//One random neighbor
			randomID = neighbours.at(randomIndex)->id;

			//First position of the vector
			if(id==0){
				//Distinct of itself, the right-side neighbor and the last neighbor
				if(randomID != id && randomID != id+1 && randomID != config->getSwarmSize()-1)
					return randomID;
				else
					continue;
			}
			else{
				//Last position of the vector
				if(id == config->getSwarmSize()-1){
					//Distinct of itself, the first neighbor and the left-side neighbor
					if(randomID != id && randomID != 0 && randomID != id-1)
						return randomID;
					else
						continue;
				}
				else{
					//Some position in within the bounds just check right- and left-side neighbors
					if(randomID != id && randomID != id+1 && randomID != id-1)
						return randomID;
					else
						continue;
				}
			}
		}
	}
	else
		return -1;
}

//double Particle::computeNewVelocity(Configuration* config, double vel, double u1, double u2, double perInf, double socInf, double pos, double additionalVal){
//	double new_vel = 0.0;
//
//	//Original PSO
//	if (config->getVelocityRule() == VEL_BASIC){
//		new_vel = vel +
//				(phi_1 * problem->getRandom01() * perInf) +
//				(phi_2 * problem->getRandom01() * socInf);
//	}
//	//Standard PSO
//	else if (config->getVelocityRule() == VEL_STANDARD){
//		new_vel = (inertia * vel) +
//				(phi_1 * problem->getRandom01() * perInf) +
//				(phi_2 * problem->getRandom01() * socInf);
//
//	}
//	//Linear PSO
//	else if (config->getVelocityRule() == VEL_LINEAR){
//		new_vel = (inertia * vel) +
//				(phi_1 * u1 * perInf) +
//				(phi_2 * u2 * socInf);
//	}
//	//Constriction coefficient PSO
//	else if (config->getVelocityRule() == VEL_CONSTRICTED){
//		new_vel = CONSTRICTION_COEFFICIENT * (vel +
//				(phi_1 * problem->getRandom01() * perInf) +
//				(phi_2 * problem->getRandom01() * socInf));
//	}
//	else if (config->getVelocityRule() == VEL_GUARAN_CONVERG){
//		//if (pbest.eval == gbest.eval)
//	}
//	//Standard 2011 PSO - Uses a Hypersphere distribution to generate new points
//	else if (config->getVelocityRule() == VEL_STANDARD2011){
//		//in this case additionalVal = (H(G||G-X||) - G-X) -> see StandarPSO Maurice Clerc for details
//		new_vel = (inertia * vel) + additionalVal;
//	}
//	//Use standard PSO
//	else
//		new_vel = (inertia * vel) +
//		(phi_1 * problem->getRandom01() * perInf) +
//		(phi_2 * problem->getRandom01() * socInf);
//
//
//	return new_vel;
//}

