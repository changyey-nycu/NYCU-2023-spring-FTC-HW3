CREATE TABLE IF NOT EXISTS Voter (
    name VARCHAR(20) PRIMARY KEY,
    groups VARCHAR(20),
    public_key bytea
);

CREATE TABLE IF NOT EXISTS Election (
    name VARCHAR(20) PRIMARY KEY, 
    groups VARCHAR(50), 
    choices VARCHAR(50), 
    end_date INTEGER
);

CREATE TABLE IF NOT EXISTS LogTable (
    id  INTEGER PRIMARY KEY,
    target VARCHAR(20) NOT NULL, 
    name VARCHAR(50), 
    election VARCHAR(50)
);