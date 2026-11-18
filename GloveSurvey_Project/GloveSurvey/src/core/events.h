/*
  events.h - things that happen, announced by one feature and handled by others.
  The announcing feature doesn't need to know who listens, so either side can be
  switched off. Who listens is listed in events.cpp.
*/
#pragma once

void evStep();          // steps: a step was detected
