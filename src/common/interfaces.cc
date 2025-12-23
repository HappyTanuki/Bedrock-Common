#include "common/interfaces.h"

bedrock::Validatable::~Validatable() = default;

template <std::uint32_t BufferSize, typename StatusType>
  requires bedrock::StatusEnum<StatusType>
bedrock::ReadWritable<BufferSize, StatusType>::~ReadWritable() = default;
