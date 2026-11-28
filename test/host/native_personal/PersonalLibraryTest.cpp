#include <gtest/gtest.h>
#include "booksync/interface/PersonalWidgets.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/storage/Index.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
class PersonalLibraryTest : public testing::Test {
 protected:
  IndexRecord record{};
  void SetUp() override { card={}; }
  void row(unsigned n) { record={}; std::snprintf(record.path.data(),record.path.size(),"/books/%05u.epub",n);std::snprintf(record.title.data(),record.title.size(),"Saved book"); }
};
TEST_F(PersonalLibraryTest, PersonalLibraryGetterAndRetirementNeverReadCard) {
  leaveLocalLibrary();
  Index saved{{"/.booksync/device.index","/.booksync/device.index.tmp","/.booksync/device.index.bak"},"/.booksync/device.a","/.booksync/device.b"};
  ASSERT_TRUE(saved.begin({7,1,0,9})); row(0); ASSERT_TRUE(saved.append(record)); ASSERT_TRUE(saved.commit());
  publishClockStatus({ClockEvidence::Synchronized,1795789920,0,{2026,11,27,5,14,32,0}});
  enterLocalLibrary(); WindowBatchRequest batch; batch.count=1;batch.windows[0]={0,1,LibraryBytes,WindowSource::OnDevice};batch.personalWidgets=1;
  ASSERT_TRUE(offlineLibrary().requestBatch(batch)); for(unsigned i=0;i<20;++i) workLocalLibrary();
  PersonalWidgetBatch result; card.events.clear(); offlineLibrary().readPersonalWidgets(result);
  EXPECT_EQ(result.requested,1); EXPECT_EQ(result.highlight.status.state,PersonalCardState::NotSaved);
  const auto publication=result.publication; EXPECT_GT(publication,0u);
  for(unsigned i=0;i<20;++i) offlineLibrary().readPersonalWidgets(result); EXPECT_TRUE(card.events.empty());
  AccountView account;account.ticket={7,9};publishAccountView(account);offlineLibrary().readPersonalWidgets(result);EXPECT_EQ(result.publication,0u);EXPECT_TRUE(card.events.empty());
  for(unsigned i=0;i<20;++i) workLocalLibrary();offlineLibrary().readPersonalWidgets(result);EXPECT_GT(result.publication,publication);
  leaveLocalLibrary();card.events.clear();offlineLibrary().readPersonalWidgets(result);EXPECT_EQ(result.publication,0u);EXPECT_TRUE(card.events.empty());publishClockStatus({});publishAccountView({});
}

}
