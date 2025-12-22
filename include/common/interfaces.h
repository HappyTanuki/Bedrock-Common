#ifndef BEDROCK_COMMON_COMMON_INTERFACES_H_
#define BEDROCK_COMMON_COMMON_INTERFACES_H_

class Validatable {
 public:
  virtual bool IsValid() = 0;
};

#endif