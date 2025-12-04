#ifndef PARALLEL_CBS_MESSAGES_H
#define PARALLEL_CBS_MESSAGES_H

/* Message tags for MPI communication */
typedef enum
{
    /* Coordinator to Worker messages */
    TAG_TASK = 100,
    /* Worker to Coordinator messages */
    TAG_CHILDREN = 101,
    /* General messages */
    TAG_SOLUTION = 102,
    /* Idle and termination messages */
    TAG_IDLE = 103,
    /* Termination message */
    TAG_TERMINATE = 104,
    /* Incumbent message */
    TAG_INCUMBENT = 105,
    /* Low-Level messages */
    TAG_LL_TASK = 200,
    /* Low-Level result message */
    TAG_LL_RESULT = 201,
    /* Low-Level terminate message */
    TAG_LL_TERMINATE = 202,
    /* Low-Level path request and response */
    TAG_LL_REQUEST = 210,
    /* Low-Level path response */
    TAG_LL_RESPONSE = 211,
    /* Data packet messages */
    TAG_DP_NODE = 300
} MessageTag;

#endif /* PARALLEL_CBS_MESSAGES_H */
