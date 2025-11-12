#include "qpi.h"

using namespace QPI;

constexpr uint64 QLOAN_CREATION_FEE = 20;
constexpr uint64 QLOAN_REMOVAL_FEE = 20;

constexpr uint64 QLOAN_MAX_DEPOSIT_ORDERS_NUM = 1024;

struct QLOAN2
{
};

struct QLOAN : public ContractBase
{
    enum class DepositOfferState : uint8_t
    {
        IDLE = 1,
        ACTIVE,
        ENGAGED
    };

    struct DepositOfferInfo
    {
        id owner;
        uint64 offerId;

        uint64 assetName;
        uint64 assetAmount;

        uint64 priceAmount;
        uint64 returnPeriodInEpochs;

        bool isPrivate;
        enum DepositOfferState state;
    };

protected:
    Collection<struct DepositOfferInfo, QLOAN_MAX_DEPOSIT_ORDERS_NUM> depositOffers;
    Collection<uint64_t, QLOAN_MAX_DEPOSIT_ORDERS_NUM> depositOffersIdsPool;
    //uint64 lastDepositOfferId;
    uint64 totalOffers;

public:
    struct placeDepositOffer_input
    {
        Asset asset;
        uint64 assetAmount;

        uint64 price;
        uint64 returnPeriodInEpochs;

        bool isPrivate;
    };

    struct placeDepositOffer_output
    {
    };

    struct placeDepositOffer_locals
    {
        struct DepositOfferInfo depositOfferInfo;
        uint64 depositOfferIdIdx;
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(placeDepositOffer)
    {
        if (qpi.invocationReward() < QLOAN_CREATION_FEE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            return;
        }
        if (qpi.invocationReward() >= QLOAN_CREATION_FEE) 
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - QLOAN_CREATION_FEE);
        }

        // Figure out new offer ID
        locals.depositOfferIdIdx = state.depositOffersIdsPool.headIndex(SELF);
        if (locals.depositOfferIdIdx == NULL_INDEX)
        {
            return;
        }

        // Check if our SC have rights on the user assets and user have sufficient amount of it
        if (qpi.transferShareOwnershipAndPossession(input.asset.assetName, input.asset.issuer, qpi.invocator(), qpi.invocator(), input.assetAmount, SELF) < 0)
        {
            return;
        }

        locals.depositOfferInfo.offerId = state.depositOffersIdsPool.element(locals.depositOfferIdIdx);
        locals.depositOfferInfo.owner = qpi.invocator();
        locals.depositOfferInfo.assetName = input.asset.assetName;
        locals.depositOfferInfo.assetAmount = input.assetAmount;
        locals.depositOfferInfo.priceAmount = input.price;
        locals.depositOfferInfo.returnPeriodInEpochs = input.returnPeriodInEpochs;
        locals.depositOfferInfo.isPrivate = false;
        locals.depositOfferInfo.state = DepositOfferState::IDLE;

        state.depositOffers.add(SELF, locals.depositOfferInfo, input.price);
        state.totalOffers++;

        // Remove ID from the pool
        state.depositOffersIdsPool.remove(locals.depositOfferIdIdx);
    }

    struct removeDepositOffer_input
    {
        uint64 offerIdx;
    };

    struct removeDepositOffer_output
    {
    };

    struct removeDepositOffer_locals
    {
        sint64 activeDepositOffersIdx;
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(removeDepositOffer)
    {
        if (qpi.invocationReward() < QLOAN_REMOVAL_FEE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            return;
        }
        if (qpi.invocationReward() >= QLOAN_REMOVAL_FEE) 
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - QLOAN_REMOVAL_FEE);
        }

        // Need to check, that such index exists and created by invocator
        locals.activeDepositOffersIdx = state.depositOffers.headIndex(SELF);
        while (locals.activeDepositOffersIdx != NULL_INDEX)
        {
            if (input.offerIdx == state.depositOffers.element(locals.activeDepositOffersIdx).offerId
                && state.depositOffers.element(locals.activeDepositOffersIdx).owner == qpi.invocator()
                && state.depositOffers.element(locals.activeDepositOffersIdx).state == DepositOfferState::IDLE)
            {
                // Put id back to the pool
                state.depositOffersIdsPool.add(SELF, input.offerIdx, -locals.activeDepositOffersIdx);
                // Remove deposit offer from the state
                state.depositOffers.remove(locals.activeDepositOffersIdx);

                qpi.transferShareOwnershipAndPossession(state.depositOffers.element(locals.activeDepositOffersIdx).assetName,
                                                        qpi.invocator(), SELF, SELF,
                                                        state.depositOffers.element(locals.activeDepositOffersIdx).assetName,
                                                        qpi.invocator());
                break;
            }
            locals.activeDepositOffersIdx = state.depositOffers.nextElementIndex(locals.activeDepositOffersIdx);
        }
    }

    struct releaseAsset_input
    {
    };

    struct releaseAsset_output
    {
    };

    struct releaseAsset_locals
    {
    };

    PUBLIC_PROCEDURE_WITH_LOCALS(releaseAsset)
    {
        if (qpi.invocationReward() < QLOAN_REMOVAL_FEE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            return;
        }
        if (qpi.invocationReward() >= QLOAN_REMOVAL_FEE) 
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - QLOAN_REMOVAL_FEE);
        }


    }

    struct getActiveDepositsOffers_input
    {
    };

    struct getActiveDepositsOffers_output
    {
        struct Deposit
        {
            uint64 offerId;

            uint64 assetName;
            uint64 assetAmount;

            uint64 priceAmount;
            uint64 returnPeriodInEpochs;

            enum DepositOfferState state;
        };
        Array<Deposit, 256> offers;
        uint64 offersAmount;
    };

    struct getActiveDepositsOffers_locals
    {
        sint64 outputOffersIdx;
        sint64 activeOffersIdx;

        getActiveDepositsOffers_output::Deposit offer;
    };

    PUBLIC_FUNCTION_WITH_LOCALS(getActiveDepositsOffers)
    {
        output.offersAmount = 0;
        locals.outputOffersIdx = 0;

        locals.activeOffersIdx = state.depositOffers.headIndex(SELF);

        while (locals.activeOffersIdx != NULL_INDEX && locals.outputOffersIdx < 256)
        {
            locals.offer.offerId = state.depositOffers.element(locals.activeOffersIdx).offerId;
            locals.offer.assetName = state.depositOffers.element(locals.activeOffersIdx).assetName;
            locals.offer.assetAmount = state.depositOffers.element(locals.activeOffersIdx).assetAmount;
            locals.offer.priceAmount = state.depositOffers.element(locals.activeOffersIdx).priceAmount;
            locals.offer.returnPeriodInEpochs = state.depositOffers.element(locals.activeOffersIdx).returnPeriodInEpochs;
            locals.offer.state = state.depositOffers.element(locals.activeOffersIdx).state;

            output.offers.set(locals.activeOffersIdx, locals.offer);

            locals.activeOffersIdx = state.depositOffers.nextElementIndex(locals.activeOffersIdx);
            locals.outputOffersIdx++;
        }
        output.offersAmount = state.totalOffers;
    }

    struct getFeesInfo_input
    {
    };

    struct getFeesInfo_output
    {
        uint64 creationFee;
    };


    PUBLIC_FUNCTION(getFeesInfo)
    {
        output.creationFee = QLOAN_CREATION_FEE;
    }

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        REGISTER_USER_PROCEDURE(placeDepositOffer, 1);
        REGISTER_USER_PROCEDURE(removeDepositOffer, 2);

        REGISTER_USER_FUNCTION(getActiveDepositsOffers, 1);
        REGISTER_USER_FUNCTION(getFeesInfo, 2);
    }

    struct BEGIN_EPOCH_locals
    {
        sint64 depositOfferIdIdx;
    };


    BEGIN_EPOCH_WITH_LOCALS()
    {
        locals.depositOfferIdIdx = 2;
        while (locals.depositOfferIdIdx < QLOAN_MAX_DEPOSIT_ORDERS_NUM + 2)
        {
            state.depositOffersIdsPool.add(SELF, locals.depositOfferIdIdx, -locals.depositOfferIdIdx);
            locals.depositOfferIdIdx++;
        }
        //state.totalOffers = state.depositOffersIdsPool.element(4);
    }

    //INITIALIZE()
    //{
    //    state.totalOffers = 99;
    //}

    PRE_ACQUIRE_SHARES()
    {
        output.allowTransfer = true;
    }

    PRE_RELEASE_SHARES()
    {
        output.allowTransfer = true;
    }
};
