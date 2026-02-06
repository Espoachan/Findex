#pragma once

#include "sqlite3.h"
#include <iostream>

class db_controller {
public:
    db_controller();
    ~db_controller();

    void init_db();
    void prepareStmt();
    void bindValues();
    void executeStmt();
    void finalizeStmt();
private:
    sqlite3 *db;
    sqlite3_stmt *stmt;
    
};