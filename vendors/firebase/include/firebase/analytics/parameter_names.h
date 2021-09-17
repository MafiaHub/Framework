// Copyright 2021 Google Inc. All Rights Reserved.

#ifndef FIREBASE_ANALYTICS_CLIENT_CPP_INCLUDE_FIREBASE_ANALYTICS_PARAMETER_NAMES_H_
#define FIREBASE_ANALYTICS_CLIENT_CPP_INCLUDE_FIREBASE_ANALYTICS_PARAMETER_NAMES_H_

/// @brief Namespace that encompasses all Firebase APIs.
namespace firebase {
/// @brief Firebase Analytics API.
namespace analytics {



/// @defgroup parameter_names Analytics Parameters
///
/// Predefined event parameter names.
///
/// Params supply information that contextualize Events. You can associate
/// up to 25 unique Params with each Event type. Some Params are suggested
/// below for certain common Events, but you are not limited to these. You
/// may supply extra Params for suggested Events or custom Params for
/// Custom events. Param names can be up to 40 characters long, may only
/// contain alphanumeric characters and underscores ("_"), and must start
/// with an alphabetic character. Param values can be up to 100 characters
/// long. The "firebase_", "google_", and "ga_" prefixes are reserved and
/// should not be used.
/// @{


/// Game achievement ID (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAchievementID, "10_matches_won"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAchievementID, "10_matches_won"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAchievementID  =
    "achievement_id";

/// The ad format (e.g. Banner, Interstitial, Rewarded, Native, Rewarded
/// Interstitial, Instream). (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAdFormat, "Banner"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAdFormat, "Banner"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAdFormat  =
    "ad_format";

/// Ad Network Click ID (string). Used for network-specific click IDs
/// which vary in format.
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAdNetworkClickID, "1234567"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAdNetworkClickID, "1234567"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAdNetworkClickID
     = "aclid";

/// The ad platform (e.g. MoPub, IronSource) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAdPlatform, "MoPub"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAdPlatform, "MoPub"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAdPlatform  =
    "ad_platform";

/// The ad source (e.g. AdColony) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAdSource, "AdColony"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAdSource, "AdColony"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAdSource  =
    "ad_source";

/// The ad unit name (e.g. Banner_03) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAdUnitName, "Banner_03"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAdUnitName, "Banner_03"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAdUnitName  =
    "ad_unit_name";

/// A product affiliation to designate a supplying company or brick and
/// mortar store location (string).
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterAffiliation, "Google Store"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterAffiliation, "Google Store"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterAffiliation  =
    "affiliation";

/// The individual campaign name, slogan, promo code, etc. Some networks
/// have pre-defined macro to capture campaign information, otherwise can
/// be populated by developer. Highly Recommended (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCampaign, "winter_promotion"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCampaign, "winter_promotion"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCampaign  =
    "campaign";

/// Character used in game (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCharacter, "beat_boss"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCharacter, "beat_boss"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCharacter  =
    "character";

/// The checkout step (1..N) (unsigned 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCheckoutStep, "1"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCheckoutStep, "1"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
///
/// <b>This constant has been deprecated.</b>
static const char*const kParameterCheckoutStep  =
    "checkout_step";

/// Some option on a step in an ecommerce flow (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCheckoutOption, "Visa"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCheckoutOption, "Visa"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
///
/// <b>This constant has been deprecated.</b>
static const char*const kParameterCheckoutOption
     = "checkout_option";

/// Campaign content (string).
static const char*const kParameterContent  = "content";

/// Type of content selected (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterContentType, "news article"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterContentType, "news article"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterContentType  =
    "content_type";

/// Coupon code used for a purchase (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCoupon, "SUMMER_FUN"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCoupon, "SUMMER_FUN"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCoupon  = "coupon";

/// Campaign custom parameter (string). Used as a method of capturing
/// custom data in a campaign. Use varies by network.
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCP1, "custom_data"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCP1, "custom_data"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCP1  = "cp1";

/// The name of a creative used in a promotional spot (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCreativeName, "Summer Sale"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCreativeName, "Summer Sale"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCreativeName  =
    "creative_name";

/// The name of a creative slot (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCreativeSlot, "summer_banner2"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCreativeSlot, "summer_banner2"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCreativeSlot  =
    "creative_slot";

/// Currency of the purchase or items associated with the event, in
/// 3-letter
/// <a href="http://en.wikipedia.org/wiki/ISO_4217#Active_codes"> ISO_4217</a> format (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterCurrency, "USD"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterCurrency, "USD"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterCurrency  =
    "currency";

/// Flight or Travel destination (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterDestination, "Mountain View, CA"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterDestination, "Mountain View, CA"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterDestination  =
    "destination";

/// The arrival date, check-out date or rental end date for the item. This
/// should be in YYYY-MM-DD format (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterEndDate, "2015-09-14"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterEndDate, "2015-09-14"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterEndDate  = "end_date";

/// Flight number for travel events (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterFlightNumber, "ZZ800"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterFlightNumber, "ZZ800"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterFlightNumber  =
    "flight_number";

/// Group/clan/guild ID (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterGroupID, "g1"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterGroupID, "g1"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterGroupID  = "group_id";

/// The index of the item in a list (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterIndex, 5),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterIndex, 5),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterIndex  = "index";

/// Item brand (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemBrand, "Google"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemBrand, "Google"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemBrand  =
    "item_brand";

/// Item category (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemCategory, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemCategory, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemCategory  =
    "item_category";

/// Item ID (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemID, "SKU_12345"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemID, "SKU_12345"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemID  = "item_id";

/// The Google <a href="https://developers.google.com/places/place-id">Place ID</a> (string) that
/// corresponds to the associated item. Alternatively, you can supply your
/// own custom Location ID.
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemLocationID, "ChIJiyj437sx3YAR9kUWC8QkLzQ"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemLocationID, "ChIJiyj437sx3YAR9kUWC8QkLzQ"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
///
/// <b>This constant has been deprecated. Use @c FirebaseAnalytics.ParameterLocationID constant instead.</b>
static const char*const kParameterItemLocationID
     = "item_location_id";

/// Item Name (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemName, "jeggings"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemName, "jeggings"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemName  =
    "item_name";

/// The list in which the item was presented to the user (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemList, "Search Results"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemList, "Search Results"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
///
/// <b>This constant has been deprecated. Use @c FirebaseAnalytics.ParameterItemListName constant instead.</b>
static const char*const kParameterItemList  =
    "item_list";

/// Item variant (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemVariant, "Black"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemVariant, "Black"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemVariant  =
    "item_variant";

/// Level in game (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterLevel, 42),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterLevel, 42),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterLevel  = "level";

/// Location (string). The Google <a href="https://developers.google.com/places/place-id">Place ID
/// </a> that corresponds to the associated event. Alternatively, you can supply your own custom
/// Location ID.
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterLocation, "ChIJiyj437sx3YAR9kUWC8QkLzQ"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterLocation, "ChIJiyj437sx3YAR9kUWC8QkLzQ"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterLocation  =
    "location";

/// The advertising or marParameter(keting, cpc, banner, email), push.
/// Highly recommended (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterMedium, "email"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterMedium, "email"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterMedium  = "medium";

/// Number of nights staying at hotel (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterNumberOfNights, 3),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterNumberOfNights, 3),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterNumberOfNights
     = "number_of_nights";

/// Number of passengers traveling (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterNumberOfPassengers, 11),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterNumberOfPassengers, 11),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterNumberOfPassengers
     = "number_of_passengers";

/// Number of rooms for travel events (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterNumberOfRooms, 2),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterNumberOfRooms, 2),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterNumberOfRooms  =
    "number_of_rooms";

/// Flight or Travel origin (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterOrigin, "Mountain View, CA"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterOrigin, "Mountain View, CA"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterOrigin  = "origin";

/// Purchase price (double).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterPrice, 1.0),
///    Parameter(kParameterCurrency, "USD"),  // e.g. $1.00 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterPrice, 1.0),
///   new Parameter(FirebaseAnalytics.ParameterCurrency, "USD"),  // e.g. $1.00 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterPrice  = "price";

/// Purchase quantity (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterQuantity, 1),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterQuantity, 1),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterQuantity  =
    "quantity";

/// Score in game (signed 64-bit integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterScore, 4200),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterScore, 4200),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterScore  = "score";

/// Current screen class, such as the class name of the UIViewController,
/// logged with screen_view event and added to every event (string).
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterScreenClass, "LoginViewController"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterScreenClass, "LoginViewController"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterScreenClass  =
    "screen_class";

/// Current screen name, such as the name of the UIViewController, logged
/// with screen_view event and added to every event (string).
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterScreenName, "LoginView"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterScreenName, "LoginView"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterScreenName  =
    "screen_name";

/// The search string/keywords used (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterSearchTerm, "periodic table"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterSearchTerm, "periodic table"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterSearchTerm  =
    "search_term";

/// Shipping cost associated with a transaction (double).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterShipping, 5.99),
///    Parameter(kParameterCurrency, "USD"),  // e.g. $5.99 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterShipping, 5.99),
///   new Parameter(FirebaseAnalytics.ParameterCurrency, "USD"),  // e.g. $5.99 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterShipping  =
    "shipping";

/// Sign up method (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterSignUpMethod, "google"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterSignUpMethod, "google"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
///
///
/// <b>This constant has been deprecated. Use Method constant instead.</b>
static const char*const kParameterSignUpMethod  =
    "sign_up_method";

/// A particular approach used in an operation; for example, "facebook" or
/// "email" in the context of a sign_up or login event.  (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterMethod, "google"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterMethod, "google"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterMethod  = "method";

/// The origin of your traffic, such as an Ad network (for example,
/// google) or partner (urban airship). Identify the advertiser, site,
/// publication, etc. that is sending traffic to your property. Highly
/// recommended (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterSource, "InMobi"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterSource, "InMobi"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterSource  = "source";

/// The departure date, check-in date or rental start date for the item.
/// This should be in YYYY-MM-DD format (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterStartDate, "2015-09-14"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterStartDate, "2015-09-14"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterStartDate  =
    "start_date";

/// Tax cost associated with a transaction (double).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterTax, 2.43),
///    Parameter(kParameterCurrency, "USD"),  // e.g. $2.43 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterTax, 2.43),
///   new Parameter(FirebaseAnalytics.ParameterCurrency, "USD"),  // e.g. $2.43 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterTax  = "tax";

/// If you're manually tagging keyword campaigns, you should use utm_term
/// to specify the keyword (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterTerm, "game"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterTerm, "game"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterTerm  = "term";

/// The unique identifier of a transaction (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterTransactionID, "T12345"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterTransactionID, "T12345"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterTransactionID  =
    "transaction_id";

/// Travel class (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterTravelClass, "business"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterTravelClass, "business"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterTravelClass  =
    "travel_class";

/// A context-specific numeric value which is accumulated automatically
/// for each event type. This is a general purpose parameter that is
/// useful for accumulating a key metric that pertains to an event.
/// Examples include revenue, distance, time and points. Value should be
/// specified as signed 64-bit integer or double. Notes: Values for
/// pre-defined currency-related events (such as @c kEventAddToCart)
/// should be supplied using double and must be accompanied by a @c
/// kParameterCurrency parameter. The valid range of accumulated values is
/// [-9,223,372,036,854.77, 9,223,372,036,854.77]. Supplying a non-numeric
/// value, omitting the corresponding @c kParameterCurrency parameter, or
/// supplying an invalid
/// <a href="https://goo.gl/qqX3J2">currency code</a> for conversion events will cause that
/// conversion to be omitted from reporting.
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterValue, 3.99),
///    Parameter(kParameterCurrency, "USD"),  // e.g. $3.99 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterValue, 3.99),
///   new Parameter(FirebaseAnalytics.ParameterCurrency, "USD"),  // e.g. $3.99 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterValue  = "value";

/// Name of virtual currency type (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterVirtualCurrencyName, "virtual_currency_name"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterVirtualCurrencyName, "virtual_currency_name"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterVirtualCurrencyName
     = "virtual_currency_name";

/// The name of a level in a game (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterLevelName, "room_1"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterLevelName, "room_1"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterLevelName  =
    "level_name";

/// The result of an operation. Specify 1 to indicate success and 0 to
/// indicate failure (unsigned integer).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterSuccess, 1),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterSuccess, 1),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterSuccess  = "success";

/// Indicates that the associated event should either extend the current
/// session or start a new session if no session was active when the event
/// was logged. Specify YES to extend the current session or to start a
/// new session; any other value will not extend or start a session.
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterExtendSession, @YES),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterExtendSession, @YES),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterExtendSession  =
    "extend_session";

/// Monetary value of discount associated with a purchase (double).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterDiscount, 2.0),
///    Parameter(kParameterCurrency, "USD"),  // e.g. $2.00 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterDiscount, 2.0),
///   new Parameter(FirebaseAnalytics.ParameterCurrency, "USD"),  // e.g. $2.00 USD
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterDiscount  =
    "discount";

/// Item Category (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemCategory2, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemCategory2, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemCategory2  =
    "item_category2";

/// Item Category (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemCategory3, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemCategory3, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemCategory3  =
    "item_category3";

/// Item Category (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemCategory4, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemCategory4, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemCategory4  =
    "item_category4";

/// Item Category (context-specific) (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemCategory5, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemCategory5, "pants"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemCategory5  =
    "item_category5";

/// The ID of the list in which the item was presented to the
/// user (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemListID, "ABC123"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemListID, "ABC123"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemListID  =
    "item_list_id";

/// The name of the list in which the item was presented to the user
/// (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterItemListName, "Related products"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterItemListName, "Related products"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItemListName  =
    "item_list_name";

/// The list of items involved in the transaction. (NSArray).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    kParameterItems : @[
///      @{Parameter(kParameterItemName, "jeggings", kParameterItemCategory : "pants"}),
///      @{Parameter(kParameterItemName, "boots", kParameterItemCategory : "shoes"}),
///    ],
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///    FirebaseAnalytics.ParameterItems : @[
///      @{Parameter(FirebaseAnalytics.ParameterItemName, "jeggings", FirebaseAnalytics.ParameterItemCategory : "pants"}),
///      @{Parameter(FirebaseAnalytics.ParameterItemName, "boots", FirebaseAnalytics.ParameterItemCategory : "shoes"}),
///    ],
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterItems  = "items";

/// The location associated with the event. Preferred to be the Google
/// <a href="https://developers.google.com/places/place-id">Place ID</a> that corresponds to the
/// associated item but could be overridden to a custom location ID
/// string.(string).
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterLocationID, "ChIJiyj437sx3YAR9kUWC8QkLzQ"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterLocationID, "ChIJiyj437sx3YAR9kUWC8QkLzQ"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterLocationID  =
    "location_id";

/// The chosen method of payment (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterPaymentType, "Visa"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterPaymentType, "Visa"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterPaymentType  =
    "payment_type";

/// The ID of a product promotion (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterPromotionID, "ABC123"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterPromotionID, "ABC123"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterPromotionID  =
    "promotion_id";

/// The name of a product promotion (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterPromotionName, "Summer Sale"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterPromotionName, "Summer Sale"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterPromotionName  =
    "promotion_name";

/// The shipping tier (e.g. Ground, Air, Next-day) selected for delivery
/// of the purchased item (string).
///
///
/// @if cpp_examples
/// @code{.cpp}
/// using namespace firebase::analytics;
/// Parameter parameters[] = {
///    Parameter(kParameterShippingTier, "Ground"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// <SWIG>
/// @if swig_examples
/// @code{.cs}
/// using Firebase.Analytics;
/// Parameter parameters[] = {
///   new Parameter(FirebaseAnalytics.ParameterShippingTier, "Ground"),
///    // ...
///  };
/// @endcode
///
/// @endif
/// </SWIG>
static const char*const kParameterShippingTier  =
    "shipping_tier";
/// @}

}  // namespace analytics
}  // namespace firebase

#endif  // FIREBASE_ANALYTICS_CLIENT_CPP_INCLUDE_FIREBASE_ANALYTICS_PARAMETER_NAMES_H_
