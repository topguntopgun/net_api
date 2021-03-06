#ifndef CPP_TEST
#define CPP_TEST

#include <string>
#include <iostream>

using namespace std;

class Stu
{
public:
	Stu(string name, int age)//:name(name),age(age)
	{
		this->name = name;
		this->age  = age;
	}

	~Stu(){}

	Stu& growup(void);
	void display(void);

private:
	string name;
	int    age;
};

class myclock
{
public:
    myclock();
    /*myclock(int h = 0, int m = 0, int s = 0):hour(h), min(m), sec(s)
        {
        
        }*/
    
	~myclock(){};
	void run(void);
	
private:
    void tick();
    void show();
    
	int hour;
	int min;
	int sec;
};

class base
{
public:
	void foo(void)
	{
		func();
	}

	virtual void func(void)
	{
		std::cout<<"base func"<<endl;
	}
};

class derive:public base
{
private:
	void func(void)
	{
		std::cout<<"derive func"<<endl;
	}
};

class
employee
{
public:
	employee(string name, int level):name(name),level(level)
	{
		cardnum = num;
		++num;
	}
	
	~employee(){}

	virtual float get_salary() = 0;
	
	int get_cardnum()
	{
		return cardnum;
	}
	
	static int get_maxnum()
	{
		return num;
	}
	
public:
	string name;
	int    level;
	int    cardnum;
	static int num;
};

int employee::num = 1000;

class technician :public employee
{
public:
	technician(string name, int time, int level = 3, float salary = 100):employee(name, level), time(time), salary(salary){}
	~technician(){}

	float get_salary()
	{
		return time*salary;
	}

public:
	int   time;
	float salary;
};

class salesman : virtual public employee
{
public:
	salesman(string name, float total, int level = 1, float ratio = 0.4):employee(name, level), ratio(ratio), amount(total){}
	~salesman(){}

	float get_salary()
	{
		return ratio*amount/100;
	}

public:
	float ratio;
	float amount;
};

class manager : virtual public employee
{
public:
	manager(string name, int level = 4, float base = 8000):employee(name, level)
	{
		this->base_salary = base;
	}
	
	~manager(){}
	
	float get_salary()
	{
		return base_salary;
	}
	
public:
	float base_salary;
};

class sales_manager : public manager, public salesman
{
public:
	sales_manager(string name, float total, int level = 3, float base = 8000, float ratio = 0.5):
		employee(name, level), manager(name, level, base), salesman(name, total, level, ratio){}
		
	~sales_manager(){}

	float get_salary()
	{
		return base_salary + ratio*amount/100;
	}	
};

void multi_inheritance();

#endif
