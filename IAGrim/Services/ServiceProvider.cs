﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using IAGrim.Backup.Azure.Service;
using IAGrim.Backup.Azure.Util;
using IAGrim.Database;
using IAGrim.Database.Interfaces;
using IAGrim.Database.Migrations;
using IAGrim.Database.Synchronizer;
using IAGrim.Parsers;
using IAGrim.Parsers.Arz;
using IAGrim.Parsers.GameDataParsing.Service;
using IAGrim.Parsers.TransferStash;
using IAGrim.Settings;
using IAGrim.UI.Controller;
using IAGrim.UI.Misc.CEF;
using IAGrim.Utilities;

namespace IAGrim.Services {
    public class ServiceProvider {
        private readonly List<object> _services = new List<object>();
        private readonly Dictionary<Type, object> _cache = new Dictionary<Type, object>();

        public ServiceProvider(List<object> services) {
            _services = services;
        }

        public T Get<T>() {
            var typeOf = typeof(T);
            if (_cache.ContainsKey(typeOf)) {
                return (T)_cache[typeOf];
            }

            foreach (var service in _services) {
                if (service.GetType() == typeOf || service.GetType().GetInterfaces().Contains(typeOf)) {
                    _cache[typeOf] = service;
                    return (T) service;
                }
            }

            throw new ArgumentException("Cannot find service of type", typeof(T).ToString());
        }

        public static ServiceProvider Initialize(ThreadExecuter threadExecuter) {
            var factory = new SessionFactory();

            // Settings should be upgraded early, it contains the language pack etc and some services depends on settings.
            var settingsService = StartupService.LoadSettingsService();
            IPlayerItemDao playerItemDao = new PlayerItemRepo(threadExecuter, factory);
            IDatabaseItemDao databaseItemDao = new DatabaseItemRepo(threadExecuter, factory);
            IDatabaseSettingDao databaseSettingDao = new DatabaseSettingRepo(threadExecuter, factory);
            var azurePartitionDao = new AzurePartitionRepo(threadExecuter, factory);
            IDatabaseItemStatDao databaseItemStatDao = new DatabaseItemStatRepo(threadExecuter, factory);
            IItemTagDao itemTagDao = new ItemTagRepo(threadExecuter, factory);
            IBuddyItemDao buddyItemDao = new BuddyItemRepo(threadExecuter, factory);
            IBuddySubscriptionDao buddySubscriptionDao = new BuddySubscriptionRepo(threadExecuter, factory);
            IRecipeItemDao recipeItemDao = new RecipeItemRepo(threadExecuter, factory);
            IItemSkillDao itemSkillDao = new ItemSkillRepo(threadExecuter, factory);
            AugmentationItemRepo augmentationItemRepo = new AugmentationItemRepo(threadExecuter, factory, new DatabaseItemStatDaoImpl(factory));
            var grimDawnDetector = new GrimDawnDetector(settingsService);
            var itemCollectionRepo = new ItemCollectionRepo(threadExecuter, factory);

            // Chicken and the egg..
            SearchController searchController = new SearchController(
                databaseItemDao,
                playerItemDao,
                new ItemStatService(databaseItemStatDao, itemSkillDao, settingsService),
                buddyItemDao,
                augmentationItemRepo,
                settingsService,
                itemCollectionRepo
            );

            List<object> services = new List<object>();
            services.Add(itemTagDao);
            services.Add(databaseItemDao);
            services.Add(databaseItemStatDao);
            services.Add(playerItemDao);
            services.Add(azurePartitionDao);
            services.Add(databaseSettingDao);
            services.Add(buddyItemDao);
            services.Add(buddySubscriptionDao);
            services.Add(itemSkillDao);
            services.Add(augmentationItemRepo);
            //services.Add(userFeedbackService);
            services.Add(settingsService);
            services.Add(grimDawnDetector);
            services.Add(recipeItemDao);
            services.Add(itemCollectionRepo);
            services.Add(searchController);

            services.Add(new ItemStatService(databaseItemStatDao, itemSkillDao, settingsService));

            var stashWriter = new SafeTransferStashWriter(settingsService);
            services.Add(new TransferStashService(databaseItemStatDao, settingsService, stashWriter));

            return new ServiceProvider(services);
        }
    }
}