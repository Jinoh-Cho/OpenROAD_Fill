#!/usr/bin/env python3
"""Render range and area evidence from the common M2 quality experiment."""
import argparse
import json
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args = parser.parse_args()
    root=args.directory.resolve()
    os.environ.setdefault('MPLCONFIGDIR',str(root/'.mplconfig'))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    rows=json.loads((root/'summary.json').read_text())
    methods=['density','lp_minvar','min_amount']
    names={'density':'Existing density_fill','lp_minvar':'MIN VAR','min_amount':'MIN AMOUNT'}
    colors={'density':'#7B8794','lp_minvar':'#2878B5','min_amount':'#E68A2E'}
    cases=[('gcd_ci_rules','GCD / upstream CI M2 rules'),
           ('gcd_local_rules','GCD / local comparison M2 rules'),
           ('tpu_1x1','TPU 1x1 / ASAP7 M2 rules')]
    fig,axes=plt.subplots(1,3,figsize=(16,4.6),constrained_layout=True)
    for ax,(case,title) in zip(axes,cases):
        ax.axvspan(30,60,color='#CBE8D5',alpha=.65,zorder=0)
        for method,y in zip(methods,[2,1,0]):
            row=next(r for r in rows if r['case']==case and r['method']==method)
            if row['status']!='ok':
                ax.text(3,y,'Infeasible at 30% lower bound',color='#A52A2A',va='center',fontsize=9)
                continue
            lo,hi=row['min_percent'],row['max_percent']
            ax.plot([lo,hi],[y,y],lw=9,color=colors[method],solid_capstyle='round')
            ax.plot(row['mean_percent'],y,'|',ms=16,mew=2,color='black')
            ax.text((lo+hi)/2,y+.23,'%.4f - %.4f%%'%(lo,hi),ha='center',fontsize=9)
        ax.set_title(title,fontsize=11)
        ax.set_yticks([2,1,0]);ax.set_yticklabels([names[m] for m in methods],fontsize=10)
        ax.set_xlim(0,75);ax.set_ylim(-.55,2.6)
        ax.set_xlabel('Sampled window density (%)')
        ax.grid(axis='x',alpha=.2)
    fig.suptitle('M2 density min-max after actual placement\n50 x 50 um windows, 12.5 um step; green band = 30-60% evaluation limits',fontsize=13)
    fig.savefig(str(root/'density_ranges.png'),dpi=200,bbox_inches='tight')
    fig.savefig(str(root/'density_ranges.svg'),bbox_inches='tight')
    plt.close(fig)
    fig,axes=plt.subplots(1,3,figsize=(14,4.5),constrained_layout=True)
    for ax,(case,title) in zip(axes,cases):
        for x,method in enumerate(methods):
            row=next(r for r in rows if r['case']==case and r['method']==method)
            if row['status']!='ok':
                ax.text(x,.12,'Infeasible',rotation=90,va='bottom',ha='center',color='#A52A2A',transform=ax.get_xaxis_transform())
                continue
            value=row['fill_area_um2']
            ax.bar(x,value,color=colors[method],width=.65)
            ax.annotate('{:,.2f}'.format(value),(x,value),xytext=(0,4),textcoords='offset points',ha='center',fontsize=9)
        ax.set_xticks([0,1,2]);ax.set_xticklabels(['Existing fill','MIN VAR','MIN AMOUNT'],fontsize=9)
        ax.set_title(title,fontsize=11);ax.set_ylabel('Added union metal area (um^2)')
        ax.set_ylim(0,ax.get_ylim()[1]*1.15);ax.grid(axis='y',alpha=.2)
    fig.suptitle('M2 added fill area inside the same comparison region',fontsize=13)
    fig.savefig(str(root/'fill_area.png'),dpi=200,bbox_inches='tight')
    fig.savefig(str(root/'fill_area.svg'),bbox_inches='tight')
    plt.close(fig)
    print('Figures saved to '+str(root))


if __name__=='__main__':
    main()
