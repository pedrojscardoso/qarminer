#include "fpgrowth.h"

FPGrowth::FPGrowth(QString filename, SupportCount minimumSupport, float minimumConfidence) {
    this->parser.setFile(filename);
    this->minimumSupport = minimumSupport;
    this->minimumConfidence = minimumConfidence;
    this->numberTransactions = 0;
    this->tree = new FPTree();




    qDebug() << "Preprocessing stage 1: parsing item names and support counts."
             << "-" << "Time complexity: O(n).";
    QPair<ItemNameHash, ItemCountHash> result;
    result = this->parser.parseItemNamesAndSupportCounts();
    this->itemNames = result.first;
    this->totalSupportCounts = result.second;
    this->calculateItemsSortedBySupportCount();
    // Let the tree know the names of the items, so it can print those instead
    // of the corresponding integers when printing the tree.
    this->tree->setItemNames(&this->itemNames);




    qDebug() << "Preprocessing stage 2: parsing transactions"
             << "-" << "Time complexity: O(n).";
    QObject::connect(&(this->parser), SIGNAL(parsedTransaction(Transaction)), this, SLOT(parsedTransaction(Transaction)));
    // The slot that parser.parseTransactions() will call, will fill the FPTree.
    this->parser.parseTransactions();
    qDebug() << "Parsed" << this->numberTransactions << "transactions.";
    qDebug() << *this->tree;




    qDebug() << "Calculating stage 1: frequent itemset generation"
             << "-" << "Time complexity: [not yet available].";
    ItemIDList orderedSuffixes = this->determineSuffixOrder();

    // Debug output.
    NamedItemIDList namedOrderedSuffixes;
    namedOrderedSuffixes.itemIDs = orderedSuffixes;
    namedOrderedSuffixes.itemNames = this->itemNames;
    qDebug() << "ordered suffixes:" << namedOrderedSuffixes << ", or: " <<  orderedSuffixes;

    QList<ItemList> frequentItemsets = this->generateFrequentItemsets(this->tree);

    // Debug output.
    qDebug() << "frequent itemsets:" << frequentItemsets.size();
    NamedItemList nfis;
    foreach (ItemList fis, frequentItemsets) {
        nfis.items = fis;
        nfis.itemNames = this->itemNames;
        qDebug() << nfis;
    }




    qDebug() << "Calculating stage 2: support & confidence"
             << "-" << "Time complexity: O(n).";
    QList<SupportCount> frequentItemsetsSupportCounts = this->calculateSupportCountsForFrequentItemsets(frequentItemsets);
    /*
    // Debug output.
    foreach (ItemList frequentItemset, frequentItemsets) {
        nfis.items = frequentItemset;
        nfis.itemNames = this->itemNames;
        qDebug() << "support(" << nfis << ") =" << (frequentItemsetsSupportCounts[frequentItemsets.indexOf(frequentItemset)] * 1.0 / this->numberTransactions);
    }
    */




    qDebug() << "Calculating stage 3: rule generation"
             << "-" << "Time complexity: [not yet available].";
    QList<AssociationRule> associationRules = this->generateAssociationRules(frequentItemsets, frequentItemsetsSupportCounts);
    qDebug() << "mined rules: ";
    foreach (AssociationRule rule, associationRules) {
        NamedAssociationRule nr;
        nr.antecedent = rule.antecedent;
        nr.consequent = rule.consequent;
        nr.confidence = rule.confidence;
        nr.itemNames = this->itemNames;
        qDebug() << "\t-" << nr;
    }
}


//------------------------------------------------------------------------------
// Protected methods.

// TODO: make this method use this->itemsSortedBySupportCount, which removes a
// lot of per-transaction calculations, at the cost of comparing with a possibly
// long list of items to find the proper order.
Transaction FPGrowth::optimizeTransaction(Transaction transaction) const {
    Transaction optimizedTransaction;
    QHash<ItemID, Item> itemIDToItem;
    QHash<SupportCount, ItemID> totalSupportToItemID;
    QSet<SupportCount> supportSet;
    QList<SupportCount> supportList;
    SupportCount totalSupportCount;

    // Fill the following variables:
    // - itemIDToItem, which maps itemIDs (key) to their corresponding items
    //   (values).
    // - supportToItemID, which maps supports (key) to all item IDs with that
    //   support (values).
    // - supportSet, which is a set of all different supports. This will be used
    //   to sort by support.
    // But discard infrequent items (i.e. if their support is smaller than the
    // minimum support).
    foreach (Item item, transaction) {
        itemIDToItem.insert(item.id, item);
        totalSupportCount = this->totalSupportCounts[item.id];

        // Discard items that are total infrequent.
        if (totalSupportCount < this->minimumSupport)
            continue;

        // Fill itemsBySupport by using QHash::insertMulti(), which allows for
        // multiple values for the same key.
        totalSupportToItemID.insertMulti(totalSupportCount, item.id);

        // Fill supportSet.
        supportSet.insert(totalSupportCount);
    }

    // It's possible that none of the items in this transaction meet or exceed
    // the minimum support.
    if (supportSet.size() == 0)
        return optimizedTransaction;

    // Sort supportSet from greater to smaller. But first convert supportSet to
    // a list supportList, because sets cannot have an order by definition.
    supportList = supportSet.toList();
    qSort(supportList.begin(), supportList.end(), qGreater<SupportCount>());

    // Store items with largest support first in the optimized transaction:
    // - first iterate over all supports in supportList, which are now sorted
    //   from greater to smaller
    // - then retrieve all items that have this support
    // - then sort these items to ensure the same order is applied to all
    //   itemsets with the same support, which minimizes the size of the FP-tree
    //   even further (i.e. maximizes density)
    // - finally, append the item to the transaction optimizedTransaction.
    foreach (SupportCount support, supportList) {
        ItemIDList itemIDs = totalSupportToItemID.values(support);
        qSort(itemIDs);
        foreach (ItemID itemID, itemIDs)
            optimizedTransaction << itemIDToItem[itemID];
    }

    return optimizedTransaction;
}

void FPGrowth::calculateItemsSortedBySupportCount() {
    QHash<SupportCount, ItemID> itemsByTotalSupport;
    QSet<SupportCount> supportSet;
    QList<SupportCount> supportList;
    SupportCount totalSupport;

    Q_ASSERT_X(this->itemsSortedByTotalSupportCount.size() == 0, "FPGrowth::getItemsSortedBySupportCount", "Should only be called once.");

    foreach (ItemID itemID, this->totalSupportCounts.keys()) {
        totalSupport = this->totalSupportCounts[itemID];

        // Fill itemsBySupport by using QHash::insertMulti(), which allows for
        // multiple values for the same key.
        itemsByTotalSupport.insertMulti(totalSupport, itemID);

        // Fill supportSet.
        supportSet.insert(totalSupport);
    }

    // Sort supportSet from smaller to greater. But first convert supportSet to
    // a list supportList, because sets cannot have an order by definition.
    supportList = supportSet.toList();
    qSort(supportList);

    // Store items with largest support first in the optimized transaction:
    // - first iterate over all supports in supportList, which are now sorted
    //   from greater to smaller
    // - then retrieve all items that have this support
    // - then sort these items to ensure the same order is applied to all
    //   itemsets with the same support, which minimizes the size of the FP-tree
    //   even further (i.e. maximizes density)
    // - finally, append the item to the transaction optimizedTransaction.
    foreach (SupportCount support, supportList) {
        ItemIDList itemIDs = itemsByTotalSupport.values(support);
        qSort(itemIDs);
        this->itemsSortedByTotalSupportCount.append(itemIDs);
    }
}

ItemIDList FPGrowth::determineSuffixOrder() const {
    return this->itemsSortedByTotalSupportCount;
}

QList<ItemList> FPGrowth::generateFrequentItemsets(FPTree* ctree, ItemList suffix) {
    NamedItemList namedSuffix;
    namedSuffix.items = suffix;
    namedSuffix.itemNames = this->itemNames;
    qDebug() << "---------------------------------generateFrequentItemsets()" << namedSuffix;


    QList<ItemList> frequentItemsets;
    QList<ItemList> prefixPaths;

    // Build a variation of the suffix, storing a mapping of item IDs to their
    // corresponding items. We'll need this in some of the calculations.
    QHash<ItemID, Item> suffixItemIDToItem;
    foreach (Item item, suffix)
        suffixItemIDToItem.insert(item.id, item);

    // First determine the suffix order for the items in this particular tree,
    // based on the list that contains *all* items in this data set, sorted by
    // support count.
    ItemIDList orderedSuffixItemIDs;
    ItemIDList itemIDsInTree = ctree->getItemIDs();
    foreach (ItemID itemID, this->itemsSortedByTotalSupportCount)
        if (itemIDsInTree.contains(itemID))
            orderedSuffixItemIDs.append(itemID);

    // Now iterate over each of the ordered suffix items and generate frequent
    // itemsets!
    foreach (ItemID suffixItemID, orderedSuffixItemIDs) {
        // Debug output.
        /*
        NamedItemID namedSuffixItemID;
        namedSuffixItemID.itemID = suffixItemID;
        namedSuffixItemID.itemNames = this->itemNames;
        if (suffix.size() == 0)
            qDebug() << "==========ROOT==========";
        qDebug() << "suffix item" << namedSuffixItemID << ctree->getItemSupport(suffixItemID) << (ctree->getItemSupport(suffixItemID) >= this->minimumSupport);
        */

        // Only if this suffix item's support meets or exceeds the minim
        // support, it will be added as a frequent itemset (appended with the
        // received suffix of course).
        SupportCount suffixItemSupport = ctree->getItemSupport(suffixItemID);
        if (suffixItemSupport >= this->minimumSupport) {
            // The current suffix item, when prepended to the received suffix,
            // is the next frequent itemset. Additionally, it will serve as the
            // next suffix.
            Item suffixItem = { suffixItemID, suffixItemSupport };
            ItemList frequentItemset;
            frequentItemset.append(suffixItem);
            frequentItemset.append(suffix);

            // Debug output.
            NamedItemList namedFrequentItemSet;
            namedFrequentItemSet.items = frequentItemset;
            namedFrequentItemSet.itemNames = this->itemNames;
            qDebug() << "\t\t\t\t new frequent itemset:" << namedFrequentItemSet;

            // Add the new frequent itemset to the list of frequent itemsets.
            frequentItemsets.append(frequentItemset);

            // Calculate the prefix paths for the current suffix item.
            prefixPaths = ctree->calculatePrefixPaths(suffixItemID);

            // Debug output.
            qDebug() << "prefix paths:";
            foreach (ItemList prefixPath, prefixPaths) {
                NamedItemList namedPrefixPath;
                namedPrefixPath.items = prefixPath;
                namedPrefixPath.itemNames = this->itemNames;
                qDebug() << "\t" << namedPrefixPath;
            }

            // Calculate the support counts for the prefix paths.
            ItemCountHash prefixPathsSupportCounts = FPTree::calculateSupportCountsForPrefixPaths(prefixPaths);

            // Remove items from the prefix paths based on the support counts of
            // the items *within* the prefix paths. Also remove the suffix item.
            QList<ItemList> filteredPrefixPaths;
            ItemList prefixPath, filteredPrefixPath;
            Item item;
            for (int i = 0; i < prefixPaths.size(); i++) {
                prefixPath = prefixPaths[i];
                for (int j = 0; j < prefixPath.size() - 1; j++) {
                    item = prefixPath[j];
                    if (prefixPathsSupportCounts[item.id] >= this->minimumSupport)
                        filteredPrefixPath.append(item);
                }
                if (filteredPrefixPath.size() > 0)
                    filteredPrefixPaths.append(filteredPrefixPath);
                filteredPrefixPath.clear();
            }

            // Debug output.
            qDebug() << "filtered prefix paths: ";
            foreach (ItemList prefixPath, filteredPrefixPaths) {
                NamedItemList namedPrefixPath;
                namedPrefixPath.items = prefixPath;
                namedPrefixPath.itemNames = this->itemNames;
                qDebug() << "\t" << namedPrefixPath;
            }


            // If no prefix paths remain after filtering, we won't be able to
            // generate any further frequent item sets.
            if (filteredPrefixPaths.size() > 0) {
                // Build the conditional FP-tree for these prefix paths, by creating
                // a new FP-tree and pretending the prefix paths are transactions.
                FPTree* cfptree = new FPTree();
                cfptree->setItemNames(&this->itemNames);
                foreach (ItemList prefixPath, filteredPrefixPaths)
                    cfptree->addTransaction(prefixPath);
                qDebug() << *cfptree;

                // Attempt to generate more frequent itemsets, with the current
                // frequent itemset as the suffix.
                frequentItemsets.append(this->generateFrequentItemsets(cfptree, frequentItemset));

                delete cfptree;
            }
        }
        else
            qDebug() << "support count of" << suffixItemID << "in the initial tree is less than minimum support";
    }
    qDebug() << "------END------------------------generateFrequentItemsets()" << suffix;

    return frequentItemsets;
}

/**
 * An exact implementation of algorithm 6.2 on page 351 in the textbook.
 */
QList<AssociationRule> FPGrowth::generateAssociationRules(QList<ItemList> frequentItemsets, QList<SupportCount> frequentItemsetsSupportCounts) {
    QList<AssociationRule> associationRules;
    QList<ItemList> consequents;

    // Iterate over all frequent itemsets.
    foreach (ItemList frequentItemset, frequentItemsets) {
        // It's only possible to generate an association rule if there are at
        // least two items in the frequent itemset.
        if (frequentItemset.size() >= 2) {
            // Generate all 1-item consequents.
            consequents.clear();
            foreach (Item item, frequentItemset) {
                ItemList consequent;
                consequent.append(item);
                consequents.append(consequent);
            }

            /*
            // Debug output.
            NamedItemList nfis;
            nfis.items = frequentItemset;
            nfis.itemNames = this->itemNames;
            qDebug() << "Generating rules for frequent itemset" << frequentItemset << nfis << " and consequents " << candidateConsequents;
            */

            // Attempt to generate association rules for this frequent itemset
            // and store the results.
            associationRules.append(this->generateAssociationRulesForFrequentItemset(frequentItemset, consequents, frequentItemsets, frequentItemsetsSupportCounts));
        }
    }
    return associationRules;
}

/**
 * A.k.a. "ap-genrules", but slightly different to fix a bug in that algorithm:
 * it accepts consequents of size 1, but doesn't generate antecedents for these,
 * instead it immediately generates consequents of size 2. Algorithm 6.3 on page
 * 352 in the textbook.
 * This variation of that algorithm fixes that.
 */
QList<AssociationRule> FPGrowth::generateAssociationRulesForFrequentItemset(ItemList frequentItemset, QList<ItemList> consequents, QList<ItemList> frequentItemsets, QList<SupportCount> frequentItemsetsSupportCounts) {
    QList<AssociationRule> associationRules;
    SupportCount frequentItemsetSupportCount, antecedentSupportCount;
    ItemList antecedent;
    float confidence;
    unsigned int k = frequentItemset.size(); // Size of the frequent itemset.
    unsigned int m = consequents[0].size(); // Size of a single consequent.

    // Iterate over all given consequents.
    foreach (ItemList consequent, consequents) {
        // Get the antecedent for the current consequent, so we effectively get
        // a candidate rule.
        antecedent = FPGrowth::getAntecedent(frequentItemset, consequent);

        // Calculate the confidence of this rule.
        frequentItemsetSupportCount = this->calculateSupportCountForFrequentItemset(frequentItemset);
        antecedentSupportCount = frequentItemsetsSupportCounts[frequentItemsets.indexOf(antecedent)];
        confidence = 1.0 * frequentItemsetSupportCount / antecedentSupportCount;

        // If the confidence is sufficiently high, we've found an association
        // rule that meets our requirements.
//        qDebug () << "confidence" << confidence << ", frequent itemset support" << frequentItemsetSupportCount << ", antecedent support" << antecedentSupportCount << ", antecedent" << antecedent << ", consequent" << consequent;
        if (confidence > this->minimumConfidence) {
            AssociationRule rule = {antecedent, consequent, confidence};
            associationRules.append(rule);
        }
        // If the confidence is not sufficiently high, delete this consequent,
        // to prevent other consequents to be generated that build upon this one
        // since those consequents would have the same insufficiently high
        // confidence in the best case.
        else
            consequents.removeAll(consequent);
    }

    // If there are still consequents left (i.e. consequents with a sufficiently
    // high confidence), and the size of the consequents still alows them to
    // be expanded, expand the consequents and attempt to generate more
    // association rules with them.
    if (consequents.size() > 0 && k > m + 1) {
        QList<ItemList> candidateConsequents = FPGrowth::generateCandidateItemsets(consequents);
        associationRules.append(this->generateAssociationRulesForFrequentItemset(frequentItemset, candidateConsequents, frequentItemsets, frequentItemsetsSupportCounts));
    }

    return associationRules;
}


//------------------------------------------------------------------------------
// Protected static methods.

/**
 * Runs FPGrowth::calculateSupportForFrequentItemset() on a list of frequent
 * itemsets.
 *
 * @see FPGrowth::calculateSupportForFrequentItemset()
 */
QList<SupportCount> FPGrowth::calculateSupportCountsForFrequentItemsets(QList<ItemList> frequentItemsets) {
    QList<SupportCount> supportCounts;
    foreach (ItemList frequentItemset, frequentItemsets)
        supportCounts.append(FPGrowth::calculateSupportCountForFrequentItemset(frequentItemset));
    return supportCounts;
}

/**
 * Given a frequent itemsets, calculate its support count.
 * Do this by finding the minimum support count of all items in the frequent
 * itemset.
 */
SupportCount FPGrowth::calculateSupportCountForFrequentItemset(ItemList frequentItemset) {
    SupportCount supportCount = 65535;
    foreach (Item item, frequentItemset)
        if (item.supportCount < supportCount)
            supportCount = item.supportCount;
    return supportCount;
}

/**
 * Build the antecedent for this candidate consequent, which are all items in
 * the frequent itemset except for those in the candidate consequent.
 */
ItemList FPGrowth::getAntecedent(ItemList frequentItemset, ItemList consequent) {
    ItemList antecedent;
    foreach (Item item, frequentItemset)
        if (!consequent.contains(item))
            antecedent.append(item);
    return antecedent;
}

/**
 * A.k.a. "apriori-gen", but without pruning because we're already working with
 * frequent itemsets, which were generated by the FP-growth algorithm. We solely
 * need this function for association rule mining, not for frequent itemset
 * generation.
 */
QList<ItemList> FPGrowth::generateCandidateItemsets(QList<ItemList> frequentItemsubsets) {
    // Phase 1: candidate generation.
    QList<ItemList> candidateItemsets;
    int allButOne = frequentItemsubsets[0].size() - 1;
    foreach (ItemList frequentItemsubset, frequentItemsubsets) {
        foreach (ItemList otherFrequentItemsubset, frequentItemsubsets) {
            if (allButOne == 0) {
                if (frequentItemsubset[0] == otherFrequentItemsubset[0]) {
                    break;
                }
            }
            else {
                // Iterate over all but the last item in this frequent itemsubset.
                for (int i = 0; i < allButOne; i++) {
                    if (frequentItemsubset[i] != otherFrequentItemsubset[i])
                        break;
                }
            }

            // The first all-but-one items of the two frequent itemsubsets
            // matched! Now generate a candidate itemset, whose:
            // - first k-1 == allButOne items are copied from the first
            //   frequent itemsubset (outer)
            // - last (k == allButOne + 1) item is copied from the second
            //   frequent itemsubset (inner)
            ItemList candidateItemset;
            candidateItemset.append(frequentItemsubset);
            candidateItemset.append(otherFrequentItemsubset[allButOne]);
            // Store this candidate set.
            candidateItemsets.append(candidateItemset);
        }
    }

    // Phase 2: candidate pruning.
    // Not necessary!

    return candidateItemsets;
}


//------------------------------------------------------------------------------
// Protected slots.

/**
  * Slot that receives a Transaction, optimizes it and adds it to the tree.
  */
void FPGrowth::parsedTransaction(Transaction transaction) {
    // Keep track of the number of transactions.
    this->numberTransactions++;

    Transaction optimizedTransaction;
    optimizedTransaction = this->optimizeTransaction(transaction);

    // It's possible that the optimized transaction has become empty if none of
    // the items in the given transaction meet or exceed the minimum support.
    if (optimizedTransaction.size() > 0)
        this->tree->addTransaction(optimizedTransaction);
}
