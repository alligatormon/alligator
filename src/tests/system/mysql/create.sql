CREATE TABLE IF NOT EXISTS `stats` (
	`id` int NOT NULL auto_increment,
	`name` varchar(255),
	`metric` varchar(255),
	`value` int,
	`percent` real,
	`del` bool,
	PRIMARY KEY  (`id`)
);

INSERT INTO stats (name, metric, value, percent, del) VALUES ("test", "mtest", 232, 0.23, false);
INSERT INTO stats (name, metric, value, percent, del) VALUES ("pest", "mpest", 12, 0.01, true);
INSERT INTO stats (name, metric, value, percent, del) VALUES ("qex", "mqex", 1214, 0.64, false);
