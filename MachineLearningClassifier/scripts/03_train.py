import argparse
import os
import uuid
from datetime import datetime
from pathlib import Path
import yaml

import benthic_models.benthic_models as ut
import pytorch_lightning as pl
import torch
import wandb
from pytorch_lightning.callbacks import ModelCheckpoint
from pytorch_lightning.callbacks.early_stopping import EarlyStopping
from pytorch_lightning.loggers import WandbLogger, TensorBoardLogger

if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser = ut.add_dataset_args(parser)
    parser = ut.add_dataloader_args(parser)
    parser = ut.add_model_args(parser)
    parser = ut.add_train_args(parser)
    parser = ut.add_program_args(parser)

    args = parser.parse_args()

    gpu_count = torch.cuda.device_count()

    uid = datetime.now().strftime("%y%m%d-%H%M") + f"-{str(uuid.uuid4())[:4]}"
    basename = f"{args.out_prefix}_{args.model}_{args.criterion}_b{args.batch_size}"
    outname = f"{basename}_f{args.fold}_{uid}"

    out_folder = Path(args.out_folder) / Path(args.dataset_name) / basename / f"f{args.fold}" / uid
    out_folder.mkdir(exist_ok=True, parents=True)

    if args.class_map:
        class_map = ut.load_class_map(args.class_map)
    else:
        class_map = {"fwd": None, "inv": None}

    if args.deterministic:
        pl.seed_everything(seed=args.global_seed)

    dm = ut.LitDataModule(
        data_folder=args.data_folder,
        dataset_name=args.dataset_name,
        csv_path=args.csv_path,
        fold=args.fold,
        label=args.label,
        label_transform=class_map["fwd"],
        imsize=args.imsize,
        batch_size=args.batch_size,
        aug=args.aug,
        load_to_memory=args.load_to_memory,
    )

    opt_args = {"name": args.opt}

    model = ut.LitModule(
        model=args.model,
        freeze_base=False,
        pretrained=True,
        criterion=args.criterion,
        opt=opt_args,
        n_classes=args.n_classes,
        lr=args.lr,
        label_transform=class_map["inv"],
    )

    checkpoint_callback = ModelCheckpoint(
        monitor="val/loss",
        dirpath=out_folder, 
        filename=f"{outname}_" + "epoch{epoch:02d}_val-loss{val/loss:.2f}",
        auto_insert_metric_name=False,
    )
    callbacks = [checkpoint_callback]

    if args.early_stopping:
        callbacks.append(
            EarlyStopping(monitor="val/loss", patience=args.early_stopping_patience)
        )

    if not args.debug:  
        logger = WandbLogger(project=args.log_dir,
                                name=outname,
                                    id=uid)
        logger.watch(model)
        wandb.config.update(args)
        #logger = TensorBoardLogger(args.log_dir, 
        #                            name=basename,
        #                            version=uid)
        #logger.log_hyperparams(vars(args))
        #logger.log_graph(model)

    else:
        logger = True

    if args.smoke_test:
        dm.setup()
        limit_train_batches = (args.batch_size * 2) / len(dm.trainset)
        limit_val_batches = (args.batch_size * 2) / len(dm.valset)
        limit_test_batches = (args.batch_size * 2) / len(dm.testset)
    else:
        limit_train_batches = 1.0
        limit_val_batches = 1.0
        limit_test_batches = 1.0

    trainer = pl.Trainer(
        max_epochs=args.max_epochs,
        min_epochs=args.min_epochs,
        logger=logger,
        log_every_n_steps=10,
        auto_lr_find=args.auto_lr,
        gpus=gpu_count,
        limit_train_batches=limit_train_batches,
        limit_val_batches=limit_val_batches,
        limit_test_batches=limit_test_batches,
        callbacks=callbacks,
        deterministic=args.deterministic,
    )

    if args.auto_lr:
        trainer.tune(model, dm)
        print(f"New lr: {model.hparams.lr}")
        wandb.config.update({"new_lr": model.hparams.lr}, allow_val_change=True)

    with open(out_folder / f'config_{uid}.yml', 'w') as f:
        f.write(yaml.dump(vars(wandb.config)['_items']))

    trainer.fit(model, dm)
    trainer.test(model, datamodule=dm, ckpt_path="best")

    dm.visualize_datasets(out_folder / f"aug-{args.aug}")

    print(
        f"Best model: {checkpoint_callback.best_model_path} | score: {checkpoint_callback.best_model_score}"
    )
    #ckpt = torch.load(checkpoint_callback.best_model_path)
    #model_out_path = out_folder / Path(checkpoint_callback.best_model_path).name

    if not args.smoke_test:
        #torch.save(ckpt, model_out_path)
        #print(f"Saved fo {model_out_path}")
        pass
