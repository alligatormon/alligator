#pragma once
int validate_path(char *path, size_t len);
int validate_domainname(char *domainname, size_t len);
int metric_name_validator(char *str, size_t sz);
int metric_value_validator(char *str, size_t sz);
int metric_label_value_validator(char *str, size_t sz);
int metric_name_validator_promstatsd(char *str, size_t sz);
int metric_name_normalizer(char *str, size_t sz);
int prometheus_metric_name_normalizer(char *str, size_t sz);
int metric_label_validator(char *str, size_t sz);
void metric_label_value_validator_normalizer(char *str, size_t sz);
