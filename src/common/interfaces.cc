#include "common/interfaces.h"

bedrock::Validatable::~Validatable() = default;

template <typename StatusType>
  requires bedrock::StatusEnum<StatusType>
bedrock::ReadWritable<StatusType>::~ReadWritable() = default;
