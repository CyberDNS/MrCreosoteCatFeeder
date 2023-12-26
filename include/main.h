#pragma once

void setAction(byte action);

void input();
void process();
void output();

void inputAwaiting();
void inputFeeding();

void processIni();
void processAwaiting();

void outputFeed();

void printDetail(uint8_t type, int value);