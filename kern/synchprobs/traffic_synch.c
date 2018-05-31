#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *intersectionSem;

static struct cv *cv_north;
static struct cv *cv_south;
static struct cv *cv_west;
static struct cv *cv_east;
static struct cv * cv_array[4];
static struct lock *intersection_lock;
volatile int car_count[4];//keep track of how many cars are in the current origin  for all 4 origins
/*
 *  north = 0, 
 *  east = 1,
 *  south = 2,
 *  west = 3,
 */

typedef struct vehicle
{
  Direction origin;
  Direction destination;
  //more fields??
} vehicle;

struct array *v_array; //array of cars that are currently in the intersection

/*
 * return true if the vehicle is turning right, false otherwise
 */
static bool
right_turn(vehicle *av)
{
  KASSERT(av != NULL);
  int temp = av->destination - av->origin;
  return (temp == -1 || temp == 3);
}

/*
 * returns true if the current car can safely enter intersection with all other cars
 * false otherwise
 */
static bool
safety_check(vehicle *cur_v)
{
  KASSERT(cur_v != NULL);

  //check the current car with every single car in the intersection see if there is a possible
  // v_array contains the cars that are currently in the intersection
  for (unsigned int i = 0; i < array_num(v_array); ++i) {
    vehicle *av_x = array_get(v_array, i); //car x in the intersection, x=1,2,3,,,n

    if (av_x == NULL) {
      //if there are no other cars -> safe
      continue;
    } else if (cur_v->origin == av_x->origin) {
      //if two cars have same origin -> safe
      continue;
    } else if ((cur_v->origin == av_x->destination) && (cur_v->destination == av_x->origin)) {
      //if two cars are going opposite direction -> safe
      continue;
    } else if ((cur_v->destination != av_x->destination) && (right_turn(cur_v) || right_turn(av_x))) {
      // if two cars have different destinations and at least 1 car is turning right -> safe
      continue;
    }
    // if non of the above is true -> not safe
    return false;
  }

  return true;
}



/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

  // intersectionSem = sem_create("intersectionSem",1);
  // if (intersectionSem == NULL) {
  //   panic("could not create intersection semaphore");
  // }

  cv_north = cv_create("north");  //0
  cv_east = cv_create("east");    //1
  cv_south = cv_create("south");  //2
  cv_west = cv_create("west");    //3
  cv_array[0] = cv_north;
  cv_array[1] = cv_east;
  cv_array[2] = cv_south;
  cv_array[3] = cv_west;
  intersection_lock = lock_create("intersection_lock");

  if (intersection_lock == NULL) {
    panic("could not create intersection lock");
  }
  if (cv_north == NULL || cv_east == NULL || cv_south == NULL || cv_west == NULL) {
    panic("could not create the 4 CVs");
  }

  v_array = array_create();
  array_init(v_array);
  for (int i = 0; i < 4; ++i) {
    //car count for every origin is 0 initially
    car_count[i] = 0;
  }

  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  // KASSERT(intersectionSem != NULL);
  // sem_destroy(intersectionSem);
  KASSERT(intersection_lock != NULL);
  KASSERT(cv_north != NULL);
  KASSERT(cv_east != NULL);
  KASSERT(cv_south != NULL);
  KASSERT(cv_west != NULL);

  lock_destroy(intersection_lock);
  cv_destroy(cv_north);
  cv_destroy(cv_east);
  cv_destroy(cv_south);
  cv_destroy(cv_west);
  array_destroy(v_array);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */
void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  // (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // P(intersectionSem);
  KASSERT(intersection_lock != NULL);
  KASSERT(cv_north != NULL);
  KASSERT(cv_east != NULL);
  KASSERT(cv_south != NULL);
  KASSERT(cv_west != NULL);

  //acquire the lock, add new car to the v_array
  lock_acquire(intersection_lock);
  struct vehicle* new_v = kmalloc(sizeof(vehicle));
  new_v->origin = origin;
  new_v->destination = destination;
  
  if (!safety_check(new_v)) {
    cv_wait(cv_array[origin], intersection_lock); // wait channel
  }
  //exit the wait channel
  lock_release(intersection_lock);
  array_add(v_array, new_v, NULL);
  car_count[origin] += 1;
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // V(intersectionSem);
  KASSERT(intersection_lock != NULL);
  KASSERT(cv_north != NULL);
  KASSERT(cv_east != NULL);
  KASSERT(cv_south != NULL);
  KASSERT(cv_west != NULL);

  lock_acquire(intersection_lock);
  car_count[origin] -= 1;
  int temp = 0;
  if (temp < 3) {
    temp += 1;
  } else {
    temp = 0;
  }
  for (unsigned int i = 0; i < array_num(v_array); ++i) {
    vehicle *temp_v = array_get(v_array, i);
    if ((temp_v->origin == origin) && (temp_v->destination == destination)) {
      array_remove(v_array, i);
    }
  }
  cv_broadcast(cv_array[temp], intersection_lock);
  lock_release(intersection_lock);
}
