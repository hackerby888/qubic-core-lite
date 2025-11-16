#include "qpi.h"

using namespace QPI;

constexpr uint64 QLOAN_CREATION_FEE = 20;
constexpr uint64 QLOAN_REMOVAL_FEE = 20;
constexpr uint64 QLOAN_RELEASE_FEE = 20;
constexpr uint64 QLOAN_ACCEPT_FEE = 20;
constexpr uint64 QLOAN_MAX_LOAN_PERIOD_IN_EPOCHS = 52;
constexpr uint64 QLOAN_MAX_INTEREST_RATE = 100;

constexpr uint64 QLOAN_MAX_LOAN_REQS_NUM = 1024;

struct QLOAN2
{
};

struct QLOAN : public ContractBase
{
    enum class LoanReqState : uint8_t
    {
        IDLE = 1,
        ACTIVE,
        PAYED,
        EXPIRED,
    };

    struct LoanOutputInfo
    {
        id borrower;
        id creditor;

        uint64 reqId;

        uint64 assetName;
        uint64 assetAmount;

        uint64 priceAmount;
        uint64 interestRate;
        uint64 debtAmount;

        uint64 returnPeriodInEpochs;
        uint64 epochsLeft;

        enum LoanReqState state;
    };

    struct LoanReqInfo
    {
        id borrower;
        uint64 reqId;

        uint64 assetName;
        id assetIssuer;
        uint64 assetAmount;

        uint64 priceAmount;
        uint64 interestRate;
        uint64 debtAmount;

        uint64 returnPeriodInEpochs;
        uint64 epochsLeft;

        bool isPrivate;

        enum LoanReqState state;

        // This field is empty while only creating loan request
        id creditor;
    };

protected:
    Collection<struct LoanReqInfo, QLOAN_MAX_LOAN_REQS_NUM> _loanReqs;
    Collection<uint64_t, QLOAN_MAX_LOAN_REQS_NUM> _loanReqsIdsPool;
    uint64 _totalReqs;
    uint64 _earnedAmount;

public:
    struct placeLoanReq_input
    {
        Asset asset;
        uint64 assetAmount;

        uint64 price;
        uint64 interestRate;
        uint64 returnPeriodInEpochs;

        bool isPrivate;
    };

    struct placeLoanReq_output
    {
    };

    struct placeLoanReq_locals
    {
        struct LoanReqInfo loanReqInfo;
        uint64 loanReqIdIdx;
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(placeLoanReq)
    {
        // Check all inputs are valid
        if (qpi.invocationReward() < QLOAN_CREATION_FEE
            || input.assetAmount == 0
            || input.returnPeriodInEpochs > QLOAN_MAX_LOAN_PERIOD_IN_EPOCHS
            || input.interestRate == 0
            || input.interestRate > QLOAN_MAX_INTEREST_RATE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            return;
        }

        if (qpi.invocationReward() > QLOAN_CREATION_FEE) 
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - QLOAN_CREATION_FEE);
            state._earnedAmount += QLOAN_CREATION_FEE;
        }

        // Figure out new req ID
        locals.loanReqIdIdx = state._loanReqsIdsPool.headIndex(SELF);
        if (locals.loanReqIdIdx == NULL_INDEX)
        {
            return;
        }

        // Check if our SC have rights on the user assets and user have sufficient amount of it
        if (qpi.transferShareOwnershipAndPossession(input.asset.assetName, input.asset.issuer, qpi.invocator(), qpi.invocator(), input.assetAmount, SELF) < 0)
        {
            return;
        }

        locals.loanReqInfo.reqId = state._loanReqsIdsPool.element(locals.loanReqIdIdx);
        locals.loanReqInfo.borrower = qpi.invocator();
        locals.loanReqInfo.creditor = qpi.invocator();
        locals.loanReqInfo.assetName = input.asset.assetName;
        locals.loanReqInfo.assetIssuer = input.asset.issuer;
        locals.loanReqInfo.assetAmount = input.assetAmount;
        locals.loanReqInfo.priceAmount = input.price;
        locals.loanReqInfo.interestRate = input.interestRate;
        locals.loanReqInfo.debtAmount = div(smul(input.price, sadd(input.interestRate, 100ULL)), 100ULL);
        locals.loanReqInfo.returnPeriodInEpochs = input.returnPeriodInEpochs;
        locals.loanReqInfo.epochsLeft = input.returnPeriodInEpochs;
        locals.loanReqInfo.isPrivate = input.isPrivate;
        locals.loanReqInfo.state = LoanReqState::IDLE;

        state._loanReqs.add(SELF, locals.loanReqInfo, input.price);
        state._totalReqs++;

        // Remove ID from the pool
        state._loanReqsIdsPool.remove(locals.loanReqIdIdx);
    }

    struct acceptLoanReq_input
    {
        uint64 reqId;
    };

    struct acceptLoanReq_output
    {
    };

    struct acceptLoanReq_locals
    {
        sint64 activeLoanReqsIdx;
        LoanReqInfo updatedLoanReq;
        bool acceptLoanReq;
    };
    
    PUBLIC_PROCEDURE_WITH_LOCALS(acceptLoanReq)
    {
        locals.acceptLoanReq = false;
        locals.activeLoanReqsIdx = state._loanReqs.headIndex(SELF);
        while (locals.activeLoanReqsIdx != NULL_INDEX)
        {
            // Find right contract by id and make sure contract in IDLE state
            if (state._loanReqs.element(locals.activeLoanReqsIdx).reqId == input.reqId
                && state._loanReqs.element(locals.activeLoanReqsIdx).state == LoanReqState::IDLE)
            {

                // Check that creditor send us right amount of money for contract
                if (qpi.invocationReward() < state._loanReqs.element(locals.activeLoanReqsIdx).priceAmount)
                {
                    qpi.transfer(qpi.invocator(), qpi.invocationReward());
                    break;
                }

                locals.updatedLoanReq = state._loanReqs.element(locals.activeLoanReqsIdx);

                locals.updatedLoanReq.creditor = qpi.invocator();
                locals.updatedLoanReq.state = LoanReqState::ACTIVE;

                state._loanReqs.replace(locals.activeLoanReqsIdx, locals.updatedLoanReq);

                // Send coins to the borrower
                qpi.transfer(state._loanReqs.element(locals.activeLoanReqsIdx).borrower,
                             state._loanReqs.element(locals.activeLoanReqsIdx).priceAmount);
                locals.acceptLoanReq = true;
                break;
            }
            locals.activeLoanReqsIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqsIdx);
        }
        if (!locals.acceptLoanReq)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
        }
    }

    struct removeLoanReq_input
    {
        uint64 reqId;
    };

    struct removeLoanReq_output
    {
    };

    struct removeLoanReq_locals
    {
        sint64 activeLoanReqIdx;
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(removeLoanReq)
    {
        if (qpi.invocationReward() < QLOAN_REMOVAL_FEE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            return;
        }
        if (qpi.invocationReward() > QLOAN_REMOVAL_FEE) 
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - QLOAN_REMOVAL_FEE);
            state._earnedAmount += QLOAN_REMOVAL_FEE;
        }

        // Need to check, that such index exists and created by invocator
        locals.activeLoanReqIdx = state._loanReqs.headIndex(SELF);
        while (locals.activeLoanReqIdx != NULL_INDEX)
        {
            if (input.reqId == state._loanReqs.element(locals.activeLoanReqIdx).reqId
                && state._loanReqs.element(locals.activeLoanReqIdx).borrower == qpi.invocator()
                && state._loanReqs.element(locals.activeLoanReqIdx).state == LoanReqState::IDLE)
            {
                // Transfer user's assets back
                qpi.transferShareOwnershipAndPossession(state._loanReqs.element(locals.activeLoanReqIdx).assetName,
                                                        state._loanReqs.element(locals.activeLoanReqIdx).assetIssuer,
                                                        SELF, SELF,
                                                        state._loanReqs.element(locals.activeLoanReqIdx).assetAmount,
                                                        qpi.invocator());

                // Put id back to the pool
                state._loanReqsIdsPool.add(SELF, input.reqId, -locals.activeLoanReqIdx);

                // Remove loan req req from the state
                state._loanReqs.remove(locals.activeLoanReqIdx);

                state._totalReqs--;
                break;
            }
            locals.activeLoanReqIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqIdx);
        }
    }

    struct releaseAsset_input
    {
        Asset assetToRelease;
        uint64 toReleaseAmount;
        uint16 dstManagingContractIndex;
    };

    struct releaseAsset_output
    {
    };

    struct releaseAsset_locals
    {
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(releaseAsset)
    {
        if (qpi.invocationReward() < QLOAN_RELEASE_FEE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            return;
        }
        if (qpi.invocationReward() > QLOAN_RELEASE_FEE) 
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - QLOAN_RELEASE_FEE);
            state._earnedAmount += QLOAN_RELEASE_FEE;
        }
        
        if (qpi.numberOfPossessedShares(input.assetToRelease.assetName,
                                        input.assetToRelease.issuer,qpi.invocator(), qpi.invocator(),
                                        SELF_INDEX, SELF_INDEX) >= input.toReleaseAmount)
        {
            qpi.releaseShares(input.assetToRelease, qpi.invocator(), qpi.invocator(),
                              input.toReleaseAmount, input.dstManagingContractIndex,
                              input.dstManagingContractIndex, 200);
        }
    }

    struct payLoanDebt_input
    {
        uint64 reqId;
    };

    struct payLoanDebt_output
    {
    };

    struct payLoanDebt_locals
    {
        sint64 activeLoanReqsIdx;
        LoanReqInfo updatedLoanReq;
        bool debtPayed;
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(payLoanDebt)
    {
        locals.debtPayed = false;
        locals.activeLoanReqsIdx = state._loanReqs.headIndex(SELF);
        while (locals.activeLoanReqsIdx != NULL_INDEX)
        {
            if (state._loanReqs.element(locals.activeLoanReqsIdx).reqId == input.reqId
                && state._loanReqs.element(locals.activeLoanReqsIdx).borrower == qpi.invocator()
                && state._loanReqs.element(locals.activeLoanReqsIdx).state == LoanReqState::ACTIVE
                && state._loanReqs.element(locals.activeLoanReqsIdx).debtAmount <= qpi.invocationReward())
            {
                // Send all money with percentage to the creditor from borrower
                qpi.transfer(state._loanReqs.element(locals.activeLoanReqsIdx).creditor, state._loanReqs.element(locals.activeLoanReqsIdx).debtAmount);

                locals.updatedLoanReq = state._loanReqs.element(locals.activeLoanReqsIdx);

                locals.updatedLoanReq.state = LoanReqState::PAYED;
                locals.updatedLoanReq.debtAmount = 0;

                // In production we should just remove this loan request
                // Replace it for now to be able to see and debug
                state._loanReqs.replace(locals.activeLoanReqsIdx, locals.updatedLoanReq);
                locals.debtPayed = true;
                break;
            }
            locals.activeLoanReqsIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqsIdx);
        }
        if (!locals.debtPayed)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
        }
    }

    struct getActiveLoanReqs_input
    {
    };

    struct getActiveLoanReqs_output
    {
        Array<LoanOutputInfo, 256> reqs;
        uint64 reqsAmount;
    };

    struct getActiveLoanReqs_locals
    {
        sint64 outputLoanReqsIdx;
        sint64 activeLoanReqsIdx;

        LoanOutputInfo tmpLoanReq;
    };

    PUBLIC_FUNCTION_WITH_LOCALS(getActiveLoanReqs)
    {
        output.reqsAmount = 0;
        locals.outputLoanReqsIdx = 0;

        locals.activeLoanReqsIdx = state._loanReqs.headIndex(SELF);

        while (locals.activeLoanReqsIdx != NULL_INDEX && locals.outputLoanReqsIdx < 256)
        {
            if (state._loanReqs.element(locals.activeLoanReqsIdx).isPrivate)
            {
                locals.tmpLoanReq.borrower = state._loanReqs.element(locals.activeLoanReqsIdx).borrower;
                locals.tmpLoanReq.creditor = state._loanReqs.element(locals.activeLoanReqsIdx).creditor;
                locals.tmpLoanReq.reqId = state._loanReqs.element(locals.activeLoanReqsIdx).reqId;
                locals.tmpLoanReq.assetName = state._loanReqs.element(locals.activeLoanReqsIdx).assetName;
                locals.tmpLoanReq.assetAmount = state._loanReqs.element(locals.activeLoanReqsIdx).assetAmount;
                locals.tmpLoanReq.priceAmount = state._loanReqs.element(locals.activeLoanReqsIdx).priceAmount;
                locals.tmpLoanReq.interestRate = state._loanReqs.element(locals.activeLoanReqsIdx).interestRate;
                locals.tmpLoanReq.debtAmount = state._loanReqs.element(locals.activeLoanReqsIdx).debtAmount;
                locals.tmpLoanReq.returnPeriodInEpochs = state._loanReqs.element(locals.activeLoanReqsIdx).returnPeriodInEpochs;
                locals.tmpLoanReq.epochsLeft = state._loanReqs.element(locals.activeLoanReqsIdx).epochsLeft;
                locals.tmpLoanReq.state = state._loanReqs.element(locals.activeLoanReqsIdx).state;

                output.reqs.set(locals.activeLoanReqsIdx, locals.tmpLoanReq);
                locals.outputLoanReqsIdx++;
                output.reqsAmount++;
            }

            locals.activeLoanReqsIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqsIdx);
        }
    }

    struct getUserActiveLoanReqs_input
    {
        id borrower;
    };

    struct getUserActiveLoanReqs_output
    {
        Array<LoanOutputInfo, 256> reqs;
        uint64 reqsAmount;
    };

    struct getUserActiveLoanReqs_locals
    {
        sint64 outputLoanReqsIdx;
        sint64 activeLoanReqsIdx;

        LoanOutputInfo tmpLoanReq;
    };

    PUBLIC_FUNCTION_WITH_LOCALS(getUserActiveLoanReqs)
    {
        output.reqsAmount = 0;
        locals.outputLoanReqsIdx = 0;

        locals.activeLoanReqsIdx = state._loanReqs.headIndex(SELF);

        while (locals.activeLoanReqsIdx != NULL_INDEX && locals.outputLoanReqsIdx < 256)
        {
            if (state._loanReqs.element(locals.activeLoanReqsIdx).borrower == input.borrower
                && state._loanReqs.element(locals.activeLoanReqsIdx).state == LoanReqState::ACTIVE)
            {
                locals.tmpLoanReq.borrower = state._loanReqs.element(locals.activeLoanReqsIdx).borrower;
                locals.tmpLoanReq.creditor = state._loanReqs.element(locals.activeLoanReqsIdx).creditor;
                locals.tmpLoanReq.reqId = state._loanReqs.element(locals.activeLoanReqsIdx).reqId;
                locals.tmpLoanReq.assetName = state._loanReqs.element(locals.activeLoanReqsIdx).assetName;
                locals.tmpLoanReq.assetAmount = state._loanReqs.element(locals.activeLoanReqsIdx).assetAmount;
                locals.tmpLoanReq.priceAmount = state._loanReqs.element(locals.activeLoanReqsIdx).priceAmount;
                locals.tmpLoanReq.interestRate = state._loanReqs.element(locals.activeLoanReqsIdx).interestRate;
                locals.tmpLoanReq.debtAmount = state._loanReqs.element(locals.activeLoanReqsIdx).debtAmount;
                locals.tmpLoanReq.returnPeriodInEpochs = state._loanReqs.element(locals.activeLoanReqsIdx).returnPeriodInEpochs;
                locals.tmpLoanReq.epochsLeft = state._loanReqs.element(locals.activeLoanReqsIdx).epochsLeft;
                locals.tmpLoanReq.state = state._loanReqs.element(locals.activeLoanReqsIdx).state;

                output.reqs.set(locals.activeLoanReqsIdx, locals.tmpLoanReq);
                locals.outputLoanReqsIdx++;
                output.reqsAmount++;
            }

            locals.activeLoanReqsIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqsIdx);
        }
    }

    struct getUserAcceptedLoanReqs_input
    {
        id creditor;
    };

    struct getUserAcceptedLoanReqs_output
    {
        Array<LoanOutputInfo, 256> reqs;
        uint64 reqsAmount;
    };

    struct getUserAcceptedLoanReqs_locals
    {
        sint64 outputLoanReqsIdx;
        sint64 activeLoanReqsIdx;

        LoanOutputInfo tmpLoanReq;
    };

    PUBLIC_FUNCTION_WITH_LOCALS(getUserAcceptedLoanReqs)
    {
        output.reqsAmount = 0;
        locals.outputLoanReqsIdx = 0;

        locals.activeLoanReqsIdx = state._loanReqs.headIndex(SELF);

        while (locals.activeLoanReqsIdx != NULL_INDEX && locals.outputLoanReqsIdx < 256)
        {
            if (state._loanReqs.element(locals.activeLoanReqsIdx).creditor == input.creditor
                && state._loanReqs.element(locals.activeLoanReqsIdx).state == LoanReqState::ACTIVE)
            {
                locals.tmpLoanReq.borrower = state._loanReqs.element(locals.activeLoanReqsIdx).borrower;
                locals.tmpLoanReq.creditor = state._loanReqs.element(locals.activeLoanReqsIdx).creditor;
                locals.tmpLoanReq.reqId = state._loanReqs.element(locals.activeLoanReqsIdx).reqId;
                locals.tmpLoanReq.assetName = state._loanReqs.element(locals.activeLoanReqsIdx).assetName;
                locals.tmpLoanReq.assetAmount = state._loanReqs.element(locals.activeLoanReqsIdx).assetAmount;
                locals.tmpLoanReq.priceAmount = state._loanReqs.element(locals.activeLoanReqsIdx).priceAmount;
                locals.tmpLoanReq.interestRate = state._loanReqs.element(locals.activeLoanReqsIdx).interestRate;
                locals.tmpLoanReq.debtAmount = state._loanReqs.element(locals.activeLoanReqsIdx).debtAmount;
                locals.tmpLoanReq.returnPeriodInEpochs = state._loanReqs.element(locals.activeLoanReqsIdx).returnPeriodInEpochs;
                locals.tmpLoanReq.epochsLeft = state._loanReqs.element(locals.activeLoanReqsIdx).epochsLeft;
                locals.tmpLoanReq.state = state._loanReqs.element(locals.activeLoanReqsIdx).state;

                output.reqs.set(locals.activeLoanReqsIdx, locals.tmpLoanReq);
                locals.outputLoanReqsIdx++;
                output.reqsAmount++;
            }

            locals.activeLoanReqsIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqsIdx);
        }
    }

    struct getFeesInfo_input
    {
    };

    struct getFeesInfo_output
    {
        uint64 creationFee;
        uint64 removalFee;
    };


    PUBLIC_FUNCTION(getFeesInfo)
    {
        output.creationFee = QLOAN_CREATION_FEE;
        output.removalFee = QLOAN_REMOVAL_FEE;
    }

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        REGISTER_USER_PROCEDURE(placeLoanReq, 1);
        REGISTER_USER_PROCEDURE(removeLoanReq, 2);
        REGISTER_USER_PROCEDURE(acceptLoanReq, 3);
        REGISTER_USER_PROCEDURE(releaseAsset, 4);
        REGISTER_USER_PROCEDURE(payLoanDebt, 5);

        REGISTER_USER_FUNCTION(getActiveLoanReqs, 1);
        REGISTER_USER_FUNCTION(getUserActiveLoanReqs, 2);
        REGISTER_USER_FUNCTION(getUserAcceptedLoanReqs, 3);
        REGISTER_USER_FUNCTION(getFeesInfo, 4);
    }

    struct BEGIN_EPOCH_locals
    {
        sint64 activeLoanReqsIdx;
        LoanReqInfo updatedLoanReq;
    };


    BEGIN_EPOCH_WITH_LOCALS()
    {
        locals.activeLoanReqsIdx = state._loanReqs.headIndex(SELF);
        while (locals.activeLoanReqsIdx != NULL_INDEX)
        {
            locals.updatedLoanReq = state._loanReqs.element(locals.activeLoanReqsIdx);

            // Check if contract already ends
            if (state._loanReqs.element(locals.activeLoanReqsIdx).epochsLeft == 0
                && state._loanReqs.element(locals.activeLoanReqsIdx).state == LoanReqState::ACTIVE)
            {
                // Check if borrower already payed the debt
                if (state._loanReqs.element(locals.activeLoanReqsIdx).debtAmount != 0)
                {
                    qpi.transferShareOwnershipAndPossession(state._loanReqs.element(locals.activeLoanReqsIdx).assetName,
                                                            state._loanReqs.element(locals.activeLoanReqsIdx).assetIssuer,
                                                            SELF, SELF,
                                                            state._loanReqs.element(locals.activeLoanReqsIdx).assetAmount,
                                                            state._loanReqs.element(locals.activeLoanReqsIdx).creditor);
                }
                locals.updatedLoanReq.state = LoanReqState::EXPIRED;
                state._loanReqs.replace(locals.activeLoanReqsIdx, locals.updatedLoanReq); 
            }
            else if (state._loanReqs.element(locals.activeLoanReqsIdx).state == LoanReqState::ACTIVE)
            {
                locals.updatedLoanReq.epochsLeft--;
                state._loanReqs.replace(locals.activeLoanReqsIdx, locals.updatedLoanReq); 
            }

            locals.activeLoanReqsIdx = state._loanReqs.nextElementIndex(locals.activeLoanReqsIdx);
        }
    }

    struct INITIALIZE_locals
    {
        sint64 loanReqIdIdx;
    };

    INITIALIZE_WITH_LOCALS()
    {
        state._totalReqs = 0;
        state._earnedAmount = 0;

        locals.loanReqIdIdx = 1;
        while (locals.loanReqIdIdx < QLOAN_MAX_LOAN_REQS_NUM + 1)
        {
            state._loanReqsIdsPool.add(SELF, locals.loanReqIdIdx, -locals.loanReqIdIdx);
            locals.loanReqIdIdx++;
        }
    }

    PRE_ACQUIRE_SHARES()
    {
        output.allowTransfer = true;
    }

    PRE_RELEASE_SHARES()
    {
        output.allowTransfer = true;
    }
};
